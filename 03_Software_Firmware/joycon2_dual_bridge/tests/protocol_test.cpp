// SPDX-License-Identifier: MIT
#include "j2_protocol.h"
#include "garden_controller.h"
#include "garden_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static void pack_stick(uint8_t* p, uint16_t x, uint16_t y) {
    p[0] = uint8_t(x); p[1] = uint8_t((x >> 8) | ((y & 15) << 4)); p[2] = uint8_t(y >> 4);
}
static void build_report(uint8_t* p, uint32_t buttons, uint16_t lx, uint16_t ly,
                         uint16_t rx = 0x7ff, uint16_t ry = 0x7ff) {
    std::memset(p,0,62);
    for(unsigned i=0;i<4;++i) p[4+i]=uint8_t(buttons>>(8*i));
    pack_stick(p+10,lx,ly); pack_stick(p+13,rx,ry);
}
int main() {
    j2::Report r{}; uint8_t p[64]{};
    CHECK(!j2::decode(nullptr,62,r));
    for(size_t n=0;n<62;++n) CHECK(!j2::decode(p,n,r));
    build_report(p,0x02810209u,1023,3072,3333,1234);
    for(unsigned first=0;first<256;++first) {
        p[0]=uint8_t(first); CHECK(j2::decode(p,62,r));
        CHECK(r.buttons==0x02810209u); CHECK(r.lx==1023 && r.ly==3072);
        CHECK(r.rx==3333 && r.ry==1234);
        CHECK(j2::decode(p,63,r));
        CHECK(r.buttons==0x02810209u && r.lx==1023 && r.ly==3072 && r.rx==3333 && r.ry==1234);
    }
    uint8_t wrapped[63]; wrapped[0]=0xa1; std::memcpy(wrapped+1,p,62);
    CHECK(j2::decode(wrapped,63,r,true)); CHECK(r.buttons==0x02810209u);
    wrapped[0]=0; CHECK(!j2::decode(wrapped,63,r,true));
    const uint8_t madv[]={0x53,0x05,1,0,3,0x7e,0x67,0x20};
    CHECK(j2::manufacturer_side(madv,8)==j2::LEFT);
    CHECK(j2::manufacturer_side(madv,7)==j2::UNKNOWN);
    // Real Joy-Con 2 Right ADV_IND capture from SDL issue 13189 (Nintendo CID
    // included); state=05 precedes the PID, unlike the compact adapter fixture.
    uint8_t real_adv[]={0x53,0x05,1,0,3,0x7e,5,0x66,0x20,0,1,0,0,0,0,0,0,0,0x0f,0,0,0,0,0,0,0};
    CHECK(j2::manufacturer_side(real_adv,sizeof(real_adv))==j2::RIGHT);
    real_adv[7]=0x67;CHECK(j2::manufacturer_side(real_adv,sizeof(real_adv))==j2::LEFT);
    real_adv[7]=0x69;CHECK(j2::manufacturer_side(real_adv,sizeof(real_adv))==j2::UNKNOWN);
    real_adv[7]=0x66;real_adv[6]=0x81;CHECK(j2::manufacturer_side(real_adv,sizeof(real_adv))==j2::RIGHT);
    CHECK(j2::product_side(0x2066)==j2::RIGHT);
    CHECK(j2::product_side(0x2069)==j2::UNKNOWN);
    for(unsigned value=0;value<4096;++value) {
        pack_stick(p+10,uint16_t(value),uint16_t(4095-value));
        CHECK(j2::stick_x(p+10)==value); CHECK(j2::stick_y(p+10)==4095-value);
        CHECK(j2::normalize(uint16_t(value),false)>=-511 && j2::normalize(uint16_t(value),false)<=511);
        CHECK(j2::normalize(uint16_t(value),true)>=-511 && j2::normalize(uint16_t(value),true)<=511);
    }
    j2::Half left{},right{};
    left.connected=right.connected=true; left.input_valid=right.input_valid=true;
    left.last_ms=right.last_ms=100;
    left.report={1u<<22,1023,3072,0x7ff,0x7ff};
    right.report={1u<<3,0x7ff,0x7ff,3072,1023};
    auto m=j2::merge(left,right,101);
    CHECK(m.lx<0 && m.ly<0 && m.rx>0 && m.ry>0);
    CHECK(m.buttons==(0x10|1)); CHECK(m.left_fresh && m.right_fresh);
    uint8_t uart[30]; j2::frame(m,0xfe,uart);
    CHECK(uart[0]==0xaa && uart[1]==0x55 && uart[2]==1 && uart[3]==0xfe && uart[29]==0x0a);
    CHECK(int16_t(j2::u16(uart+4))==m.lx && int16_t(j2::u16(uart+6))==m.ly);
    unsigned sum=0;for(unsigned i=2;i<28;++i)sum+=uart[i];CHECK(uint8_t(sum)==uart[28]);
    for(unsigned i=16;i<28;++i)CHECK(uart[i]==0);
    // Real FPGA input helper: movement from the left and actions from the right
    // must survive one merged player frame and never alternate player IDs.
    GardenController g{};garden_controller_init(&g,1);
    garden_controller_feed(&g,101,m.lx,m.ly,m.buttons,m.dpad,m.misc);
    CHECK((garden_controller_poll(&g,101)&(GARDEN_INPUT_LEFT|GARDEN_INPUT_UP|GARDEN_INPUT_SLOW))==
          (GARDEN_INPUT_LEFT|GARDEN_INPUT_UP|GARDEN_INPUT_SLOW));
    CHECK(garden_controller_take_events(&g,101)==GARDEN_INPUT_BOMB);
    garden_controller_feed(&g,117,m.lx,m.ly,m.buttons,m.dpad,m.misc);
    CHECK(garden_controller_take_events(&g,117)==0);
    left.report.buttons=1u<<8;right.report.buttons=(1u<<2)|(1u<<9);
    left.last_ms=right.last_ms=133;m=j2::merge(left,right,133);
    garden_controller_feed(&g,133,m.lx,m.ly,m.buttons,m.dpad,m.misc);
    CHECK(garden_controller_take_events(&g,133)==
          (GARDEN_INPUT_RESTART|GARDEN_INPUT_PAUSE|GARDEN_INPUT_TOGGLE_DEMO));
    // A dropped half only releases its own controls, not the other half.
    right.connected=false;m=j2::merge(left,right,134);
    CHECK(m.lx<0 && m.ly<0 && m.buttons==0 && m.misc==2 && !m.right_fresh);
    left.last_ms=0xfffffff0u;CHECK(j2::fresh(left,0x20u));CHECK(!j2::fresh(left,0xb8u));
    m=j2::merge(left,right,0xb8u);CHECK(m.lx==0 && m.buttons==0 && m.misc==0);
    // Reserved/unowned bits from a half cannot press the opposite half's keys.
    left.last_ms=right.last_ms=500;right.connected=true;
    left.report.buttons=j2::RIGHT_MASK;right.report.buttons=j2::LEFT_MASK;
    m=j2::merge(left,right,500);CHECK(m.buttons==0 && m.misc==0 && m.dpad==0);
    // Decode axis endpoints without mistaking a single zero axis for no stick.
    left.report={0,0,2048,0x7ff,0x7ff};right.connected=false;
    m=j2::merge(left,right,500);CHECK(m.lx==-511 && m.ly==0);
    for(unsigned seq=0;seq<256;++seq){j2::frame(m,uint8_t(seq),uart);CHECK(uart[2]==1 && uart[3]==seq);}
    // Either physical half can drive the existing LX-only game on its own.
    left={};right={};right.connected=right.input_valid=true;right.last_ms=1000;
    right.report={0,0x7ff,0x7ff,3072,3072};
    m=j2::merge(left,right,1000);
    CHECK(m.movement_source==2 && m.lx==256 && m.ly==-256 && m.rx==256);
    j2::frame(m,0,uart);garden_controller_init(&g,1);
    garden_controller_feed(&g,1000,int16_t(j2::u16(uart+4)),int16_t(j2::u16(uart+6)),j2::u16(uart+12),uart[14],uart[15]);
    CHECK(garden_controller_poll(&g,1000)==(GARDEN_INPUT_RIGHT|GARDEN_INPUT_UP));
    right.report.rx=right.report.ry=2048;m=j2::merge(left,right,1001);
    garden_controller_feed(&g,1001,m.lx,m.ly,m.buttons,m.dpad,m.misc);
    CHECK(garden_controller_poll(&g,1001)==0 && m.movement_source==0);
    left.connected=left.input_valid=true;left.last_ms=1000;
    left.report={0,3248,2368,0x7ff,0x7ff};right.report={0,0x7ff,0x7ff,2448,848};
    m=j2::merge(left,right,1002);CHECK(m.lx==100 && m.ly==300 && m.movement_source==2);
    left.report={0,3072,2048,0x7ff,0x7ff};right.report={0,0x7ff,0x7ff,1024,2048};
    m=j2::merge(left,right,1002);CHECK(m.lx==256 && m.ly==0 && m.movement_source==1);
    left.connected=false;m=j2::merge(left,right,1002);CHECK(m.lx==-256 && m.movement_source==2);
    left.connected=true;left.last_ms=1200;
    m=j2::merge(left,right,1200);CHECK(m.lx==256 && !m.right_fresh && m.movement_source==1);
    left.report={1u<<17,2048,2048,0x7ff,0x7ff};right.last_ms=1200;
    right.report={0,0x7ff,0x7ff,2048,2048};
    m=j2::merge(left,right,1200);CHECK(m.lx==0 && m.ly==0 && m.dpad==0 && m.movement_source==0);
    garden_controller_init(&g,1);garden_controller_feed(&g,1200,m.lx,m.ly,m.buttons,m.dpad,m.misc);
    CHECK(garden_controller_poll(&g,1200)==0 && garden_controller_take_events(&g,1200)==GARDEN_INPUT_PURE_TEST);
    // Exercise each half's full set of actions through the real game helper.
    struct Binding {j2::Side side;unsigned bit;uint16_t held,event;};
    const Binding bindings[]={
        {j2::LEFT,18,0,GARDEN_INPUT_BOMB},{j2::LEFT,16,0,GARDEN_INPUT_RESTART},
        {j2::LEFT,17,0,GARDEN_INPUT_PURE_TEST},{j2::LEFT,19,0,GARDEN_INPUT_BACKEND},
        {j2::LEFT,22,GARDEN_INPUT_SLOW,0},{j2::LEFT,8,0,GARDEN_INPUT_TOGGLE_DEMO},
        {j2::LEFT,21,0,0},{j2::LEFT,20,0,0},{j2::LEFT,23,0,0},{j2::LEFT,11,0,0},
        {j2::RIGHT,3,0,GARDEN_INPUT_BOMB},{j2::RIGHT,2,0,GARDEN_INPUT_RESTART},
        {j2::RIGHT,6,GARDEN_INPUT_SLOW,0},{j2::RIGHT,7,0,0},
        {j2::RIGHT,10,0,0},{j2::RIGHT,9,0,GARDEN_INPUT_PAUSE},
        {j2::RIGHT,5,0,0},{j2::RIGHT,4,0,0},
        {j2::RIGHT,1,0,GARDEN_INPUT_PURE_TEST},{j2::RIGHT,0,0,GARDEN_INPUT_BACKEND}
    };
    for(const auto& binding:bindings) {
        left={};right={};j2::Half& half=binding.side==j2::LEFT?left:right;
        half.connected=half.input_valid=true;half.last_ms=2000;
        half.report={1u<<binding.bit,2048,2048,2048,2048};
        m=j2::merge(left,right,2000);garden_controller_init(&g,1);
        garden_controller_feed(&g,2000,m.lx,m.ly,m.buttons,m.dpad,m.misc);
        CHECK(garden_controller_poll(&g,2000)==binding.held);
        CHECK(garden_controller_take_events(&g,2000)==binding.event);
        garden_controller_feed(&g,2016,m.lx,m.ly,m.buttons,m.dpad,m.misc);
        CHECK(garden_controller_take_events(&g,2016)==0);
        half.connected=false;m=j2::merge(left,right,2017);
        garden_controller_feed(&g,2017,m.lx,m.ly,m.buttons,m.dpad,m.misc);
        CHECK(garden_controller_poll(&g,2017)==0 && garden_controller_take_events(&g,2017)==0);
    }
    std::printf("PASS %u checks: native packets, wrap bytes, axes, merged protocol, actual game input, disconnects\n",checks);
}
