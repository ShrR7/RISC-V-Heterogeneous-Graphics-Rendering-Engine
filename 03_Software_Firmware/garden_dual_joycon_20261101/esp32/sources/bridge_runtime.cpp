// SPDX-License-Identifier: MIT
#include "bridge_runtime.h"
#include "j2_protocol.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_system.h>
#include <algorithm>
#include <atomic>
#include <cctype>

static const char* BUILD_ID = "20261031-j2-dual-nimble252-r7";
static const char* SERVICE_UUID = "ab7de9be-89fe-49ad-828f-118f09df7fd0";
static const char* INPUT_UUID = "ab7de9be-89fe-49ad-828f-118f09df7fd2";
static const char* COMMAND_UUID = "649d4ac9-8eb7-4e6c-af44-1ea54fe5f005";
static const uint8_t CALIBRATION_READ[16] = {
    0x21,0x01,0x00,0x10,0x00,0x18,0x00,0x00,0xd0,0x3d,0x06,0,0,0,0,0
};
static const uint8_t INFO_READ[16] = {
    0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7e,0,0,0x00,0x30,0x01,0
};
static const uint8_t PLAYER_LEDS[12] = {0x09,0x91,0x01,0x07,0,4,0,0,1,0,0,0};
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static j2::Half halves[2];
static std::atomic<NimBLEClient*> peers[2] = {};
static NimBLERemoteCharacteristic* commands[2] = {};
static NimBLEAddress known[2];
static bool known_valid[2] = {};
static std::atomic<bool> saved[2] = {};
static Preferences preferences;
static std::atomic<uint32_t> tx_frames{0}, tx_max_gap_ms{0};
static std::atomic<uint32_t> scan_seen{0}, scan_results{0}, scan_failures{0};
static bool pending = false;
static bool connecting = false;
static NimBLEAddress pending_address;
static j2::Side pending_side = j2::UNKNOWN;

static const char* side_name(unsigned side) { return side == 0 ? "LEFT" : "RIGHT"; }
static j2::Half snapshot(unsigned side) {
    portENTER_CRITICAL(&state_lock);
    j2::Half value = halves[side];
    portEXIT_CRITICAL(&state_lock);
    return value;
}
static int slot_for_client(NimBLEClient* client) {
    for(unsigned i=0;i<2;++i) if(peers[i].load()==client) return int(i);
    return -1;
}
static void neutralize(unsigned side) {
    portENTER_CRITICAL(&state_lock);
    halves[side].connected = false;
    halves[side].input_valid = false;
    halves[side].report = {};
    portEXIT_CRITICAL(&state_lock);
}
class ClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* client, int reason) override {
        int side=slot_for_client(client);
        if(side>=0) {
            neutralize(unsigned(side)); saved[side]=false;
            Serial.printf("[J2_DISCONNECTED] side=%s reason=%d\n",side_name(side),reason);
        }
    }
};
static ClientCallbacks client_callbacks;

static j2::Side advertised_side(const NimBLEAdvertisedDevice* device) {
    for(unsigned i=0;i<device->getManufacturerDataCount();++i) {
        std::string data=device->getManufacturerData(i);
        j2::Side side=j2::manufacturer_side(reinterpret_cast<const uint8_t*>(data.data()),data.size());
        if(side!=j2::UNKNOWN) return side;
    }
    for(unsigned i=0;i<2;++i) {
        portENTER_CRITICAL(&state_lock);
        bool valid=known_valid[i];NimBLEAddress address=known[i];
        portEXIT_CRITICAL(&state_lock);
        if(valid && device->getAddress()==address) return j2::Side(i);
    }
    std::string name=device->getName();
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(name.find("joy-con 2")==std::string::npos && name.find("joycon 2")==std::string::npos &&
       name.find("joy con 2")==std::string::npos) return j2::UNKNOWN;
    if(name.find("left")!=std::string::npos || name.find("(l)")!=std::string::npos) return j2::LEFT;
    if(name.find("right")!=std::string::npos || name.find("(r)")!=std::string::npos) return j2::RIGHT;
    return j2::UNKNOWN;
}
class ScanCallbacks : public NimBLEScanCallbacks {
    void onDiscovered(const NimBLEAdvertisedDevice* device) override {
        ++scan_seen;
        std::string name=device->getName();
        std::string lowered=name;
        std::transform(lowered.begin(),lowered.end(),lowered.begin(),[](unsigned char c){return char(std::tolower(c));});
        bool interesting=lowered.find("joy")!=std::string::npos || lowered.find("switch")!=std::string::npos;
        for(unsigned i=0;i<device->getManufacturerDataCount();++i) {
            std::string data=device->getManufacturerData(i);
            if(data.size()>=2 && j2::u16(reinterpret_cast<const uint8_t*>(data.data()))==0x0553) interesting=true;
        }
        static uint32_t last_advert_log=0;
        uint32_t now=millis();
        if(interesting && uint32_t(now-last_advert_log)>=1000) {
            last_advert_log=now;
            Serial.printf("[J2_ADVERTISEMENT] name=%s side=%u rssi=%d\n",name.c_str(),
                          unsigned(advertised_side(device)),device->getRSSI());
        }
    }
    void onResult(const NimBLEAdvertisedDevice* device) override {
        ++scan_results;
        j2::Side side=advertised_side(device);
        bool nintendo=device->isAdvertisingService(NimBLEUUID(SERVICE_UUID));
        if(side==j2::UNKNOWN && !nintendo) return;
        if(side!=j2::UNKNOWN && snapshot(side).connected) return;
        portENTER_CRITICAL(&state_lock);
        if(!pending && !connecting) {
            pending_address=device->getAddress(); pending_side=side; pending=true;
        }
        portEXIT_CRITICAL(&state_lock);
    }
};
static ScanCallbacks scan_callbacks;

static void input_notify(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool) {
    int side=slot_for_client(characteristic->getClient());
    if(side<0) return;
    j2::Report report{};
    if(!j2::decode(data,length,report)) {
        portENTER_CRITICAL(&state_lock);
        uint32_t bad=++halves[side].bad_reports;
        portEXIT_CRITICAL(&state_lock);
        if(bad<=3) Serial.printf("[J2_BAD_REPORT] side=%s bytes=%u first=%02x\n",side_name(side),
                                unsigned(length),length?data[0]:0);
        return;
    }
    uint32_t now=millis();
    portENTER_CRITICAL(&state_lock);
    halves[side].report=report;
    halves[side].last_ms=now;
    halves[side].input_valid=true;
    uint32_t count=++halves[side].reports;
    portEXIT_CRITICAL(&state_lock);
    if(count<=2) {
        Serial.printf("[J2_INPUT] side=%s bytes=%u buttons=%08lx sticks=%u,%u/%u,%u\n",
                      side_name(side),unsigned(length),(unsigned long)report.buttons,
                      report.lx,report.ly,report.rx,report.ry);
    }
}
static NimBLERemoteCharacteristic* find_characteristic(NimBLEClient* client,const char* uuid) {
    for(auto* service:client->getServices()) {
        if(auto* characteristic=service->getCharacteristic(NimBLEUUID(uuid))) return characteristic;
    }
    return nullptr;
}
static j2::Side pnp_side(NimBLEClient* client) {
    auto* service=client->getService(NimBLEUUID(uint16_t(0x180a)));
    if(!service) return j2::UNKNOWN;
    auto* characteristic=service->getCharacteristic(NimBLEUUID(uint16_t(0x2a50)));
    if(!characteristic) return j2::UNKNOWN;
    NimBLEAttValue value=characteristic->readValue();
    if(value.size()<7 || j2::u16(value.data()+1)!=0x057e) return j2::UNKNOWN;
    return j2::product_side(j2::u16(value.data()+3));
}
static bool connect_half(const NimBLEAddress& address,j2::Side hint) {
    unsigned provisional = hint!=j2::UNKNOWN ? unsigned(hint) : (snapshot(0).connected?1u:0u);
    if(snapshot(provisional).connected) return false;
    NimBLEClient* client=peers[provisional].load();
    if(!client) {
        client=NimBLEDevice::createClient();
        if(!client) return false;
        peers[provisional].store(client);
        client->setClientCallbacks(&client_callbacks,false);
        client->setConnectTimeout(5000);
        client->setConnectionParams(12,24,0,200);
    }
    Serial.printf("[J2_CONNECT] hint=%s peer=%s type=%u\n",hint==j2::UNKNOWN?"UNKNOWN":side_name(hint),
                  address.toString().c_str(),address.getType());
    if(!client->connect(address,true,false,true)) {
        Serial.printf("[J2_CONNECT_FAILED] error=%d\n",client->getLastError());
        return false;
    }
    // getServices(false) returns ONLY cached services in NimBLE 2.5.2.
    // A prior filtered PnP lookup must not hide Nintendo's other services.
    const auto& services=client->getServices(true);
    Serial.printf("[J2_SERVICES] count=%u\n",unsigned(services.size()));
    if(services.empty()) {client->disconnect();return false;}
    j2::Side side=pnp_side(client);
    if(side==j2::UNKNOWN) side=hint;
    if(side==j2::UNKNOWN || unsigned(side)!=provisional) {
        Serial.printf("[J2_SIDE_RETRY] expected=%u actual=%u\n",provisional,unsigned(side));
        if(side!=j2::UNKNOWN) {
            portENTER_CRITICAL(&state_lock);known[side]=address;known_valid[side]=true;portEXIT_CRITICAL(&state_lock);
        }
        client->disconnect();
        return false;
    }
    auto* input=find_characteristic(client,INPUT_UUID);
    auto* command=find_characteristic(client,COMMAND_UUID);
    if(!input || !command || !input->canNotify() || client->getMTU()<65) {
        Serial.printf("[J2_GATT_FAILED] input=%u command=%u mtu=%u\n",input!=nullptr,command!=nullptr,client->getMTU());
        client->disconnect();return false;
    }
    portENTER_CRITICAL(&state_lock);
    halves[side].connected=true;halves[side].input_valid=false;
    portEXIT_CRITICAL(&state_lock);
    commands[side]=command;
    if(!input->subscribe(true,input_notify)) {
        Serial.println("[J2_GATT_FAILED] subscribe");client->disconnect();return false;
    }
    bool response=!command->canWriteNoResponse();
    if(!command->writeValue(CALIBRATION_READ,sizeof(CALIBRATION_READ),response)) {
        Serial.println("[J2_GATT_FAILED] calibration_read");client->disconnect();return false;
    }
    delay(60);
    bool info_sent=command->writeValue(INFO_READ,sizeof(INFO_READ),response);
    delay(60);
    bool leds_sent=command->writeValue(PLAYER_LEDS,sizeof(PLAYER_LEDS),response);
    portENTER_CRITICAL(&state_lock);known[side]=address;known_valid[side]=true;portEXIT_CRITICAL(&state_lock);
    saved[side]=false;
    Serial.printf("[J2_CONNECTED] side=%s mtu=%u calibration_read=sent info_read=%u leds=%u player=1\n",
                  side_name(side),client->getMTU(),info_sent,leds_sent);
    return true;
}

static void uart_task(void*) {
    TickType_t wake=xTaskGetTickCount();
    uint32_t previous=millis();uint8_t sequence=0;
    for(;;) {
        uint32_t now=millis();
        uint32_t gap=now-previous;if(gap>tx_max_gap_ms.load())tx_max_gap_ms.store(gap);previous=now;
        j2::Half left,right;
        portENTER_CRITICAL(&state_lock);left=halves[0];right=halves[1];portEXIT_CRITICAL(&state_lock);
        j2::Merged merged=j2::merge(left,right,now);
        uint8_t frame[j2::FRAME_BYTES];j2::frame(merged,sequence++,frame);
        Serial2.write(frame,sizeof(frame));++tx_frames;
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(16));
    }
}

void bridge_setup() {
    Serial.begin(115200);delay(250);
    Serial.printf("\n[J2_BOOT] build=%s reset=%d ESP32-only TX2=17 baud=115200 bytes=30 player=1\n",BUILD_ID,int(esp_reset_reason()));
    Serial2.begin(115200,SERIAL_8N1,-1,17);
    preferences.begin("j2-dual",false);
    for(unsigned side=0;side<2;++side) {
        String text=preferences.getString(side==0?"left":"right","");
        uint8_t type=preferences.getUChar(side==0?"left_type":"right_type",0);
        if(text.length()==17) {known[side]=NimBLEAddress(text.c_str(),type);known_valid[side]=true;}
    }
    if(!NimBLEDevice::init("Ti60 JoyCon2 Dual Bridge")) {
        Serial.println("[J2_FATAL] NimBLE init failed");while(true)delay(1000);
    }
    NimBLEDevice::setMTU(247);
    NimBLEDevice::setSecurityAuth(false,false,false);
    NimBLEScan* scan=NimBLEDevice::getScan();
    scan->setScanCallbacks(&scan_callbacks,true);
    // Receive complete legacy advertisements immediately; do not wait for
    // scan-response names from Nintendo's proprietary peripheral.
    scan->setActiveScan(false);scan->setInterval(80);scan->setWindow(60);
    scan->setMaxResults(0);
    if(xTaskCreate(uart_task,"j2-uart",4096,nullptr,2,nullptr)!=pdPASS) {
        Serial.println("[J2_FATAL] UART task failed");while(true)delay(1000);
    }
    Serial.printf("[J2_READY] NimBLE=%s heap=%lu; hold SYNC on LEFT then RIGHT\n",
                  NimBLEDevice::getVersion(),(unsigned long)ESP.getFreeHeap());
}

void bridge_loop() {
    static uint32_t next_connect=0,last_status=0;
    bool take=false;j2::Side side=j2::UNKNOWN;NimBLEAddress address;
    uint32_t clock=millis();
    portENTER_CRITICAL(&state_lock);
    if(pending && int32_t(clock-next_connect)>=0) {
        address=pending_address;side=pending_side;pending=false;take=true;connecting=true;
    }
    portEXIT_CRITICAL(&state_lock);
    if(take) {
        NimBLEDevice::getScan()->stop();
        connect_half(address,side);
        next_connect=millis()+1000;
        portENTER_CRITICAL(&state_lock);connecting=false;portEXIT_CRITICAL(&state_lock);
    }
    uint32_t now=millis();
    j2::Half left=snapshot(0),right=snapshot(1);
    static uint32_t previous_buttons[2]={};
    for(unsigned i=0;i<2;++i) {
        j2::Half value=i==0?left:right;
        uint32_t buttons=j2::fresh(value,now) ? value.report.buttons & (i==0?j2::LEFT_MASK:j2::RIGHT_MASK) : 0;
        if(buttons!=previous_buttons[i]) {
            Serial.printf("[J2_BUTTONS] side=%s raw=%08lx\n",side_name(i),(unsigned long)buttons);
            previous_buttons[i]=buttons;
        }
        if(value.input_valid && value.connected && !saved[i] && known_valid[i]) {
            preferences.putString(i==0?"left":"right",known[i].toString().c_str());
            preferences.putUChar(i==0?"left_type":"right_type",known[i].getType());saved[i]=true;
        }
        if(value.connected && !value.input_valid && commands[i]) {
            static uint32_t retried[2]={};
            if(uint32_t(now-retried[i])>=2000) {
                retried[i]=now;
                commands[i]->writeValue(CALIBRATION_READ,sizeof(CALIBRATION_READ),!commands[i]->canWriteNoResponse());
            }
        }
    }
    if(!NimBLEDevice::getScan()->isScanning()) {
        if(!left.connected || !right.connected) {
            if(!NimBLEDevice::getScan()->start(5000,false,true)) ++scan_failures;
        }
    }
    if(uint32_t(now-last_status)>=500) {
        last_status=now;j2::Merged m=j2::merge(left,right,now);
        Serial.printf("[J2_STATE] ms=%lu L=%u R=%u Ln=%lu Rn=%lu Lbad=%lu Rbad=%lu LX=%d LY=%d RX=%d RY=%d src=%u btn=%04x dpad=%02x misc=%02x tx=%lu gap=%lu heap=%lu adv=%lu scan=%lu scanfail=%lu\n",
            (unsigned long)now,m.left_fresh,m.right_fresh,(unsigned long)left.reports,(unsigned long)right.reports,
            (unsigned long)left.bad_reports,(unsigned long)right.bad_reports,m.lx,m.ly,m.rx,m.ry,
            m.movement_source,m.buttons,m.dpad,m.misc,(unsigned long)tx_frames.load(),(unsigned long)tx_max_gap_ms.load(),(unsigned long)ESP.getFreeHeap(),
            (unsigned long)scan_seen.load(),(unsigned long)scan_results.load(),(unsigned long)scan_failures.load());
    }
    delay(5);
}
