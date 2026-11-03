/* SPDX-License-Identifier: MIT. Board reads only; no Flash erase/03_Software_Firmware/status-write. */
#include <stdint.h>
#include "package.h"
#ifndef MANIFEST_FLASH_OFFSET
#define MANIFEST_FLASH_OFFSET 0x00400000U
#endif
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SPI 0xF8014000U
#define UART 0xF8010000U
#define MTIME_LOW 0xF8B0BFF8U
#define HZ 100000000U
volatile uint32_t boot_status[8];
static BootPackage package;
static void barrier(void)
{__asm__ volatile("fence iorw,iorw\n.word 0x500F\nfence iorw,iorw":::"memory");}
static int wait_bits(uint32_t address,uint32_t mask,uint32_t value)
{
    uint32_t start=REG(MTIME_LOW);
    do {if((REG(address)&mask)!=value)return 0;} while((uint32_t)(REG(MTIME_LOW)-start)<HZ*2U);
    return -1;
}
static void putc_uart(char c)
{if(!wait_bits(UART+4U,0x00FF0000U,0U))REG(UART)=(uint8_t)c;}
static void text(const char *s){while(*s)putc_uart(*s++);}
static void hex(uint32_t value)
{for(int b=28;b>=0;b-=4)putc_uart("0123456789ABCDEF"[(value>>b)&15U]);}
static void field(const char *name,uint32_t value){text(name);text("0x");hex(value);}
static void fatal(uint32_t code,uint32_t detail) __attribute__((noreturn));
static void fatal(uint32_t code,uint32_t detail)
{
    boot_status[1]=code;boot_status[2]=detail;
    text("[BOOT_FAIL] ");field("code=",code);field(" detail=",detail);text("\r\n");
    for(;;)__asm__ volatile("wfi");
}
void boot_trap(uint32_t cause,uint32_t epc){fatal(0xE000U|cause,epc);}
static void spi_cmd(uint32_t cmd)
{if(wait_bits(SPI+4U,0xFFFFU,0U))fatal(0x101U,cmd);REG(SPI)=cmd;}
static uint8_t spi_read(void)
{spi_cmd(0x200U);if(wait_bits(SPI+4U,0xFFFF0000U,0U))fatal(0x102U,0);return (uint8_t)REG(SPI);}
static void spi_select(void){spi_cmd(0x880U);}
static void spi_deselect(void){spi_cmd(0x800U);}
static void spi_write(uint8_t value){spi_cmd(0x100U|value);}
static void spi_init(void)
{
    REG(SPI+8U)=0U;REG(SPI+0x20U)=2U;REG(SPI+0x24U)=5U;
    REG(SPI+0x28U)=2U;REG(SPI+0x2CU)=7U;
    spi_deselect();
}
static void spi_copy(uint32_t offset,uint8_t *dst,uint32_t bytes)
{
    spi_select();spi_write(3U);spi_write((uint8_t)(offset>>16));
    spi_write((uint8_t)(offset>>8));spi_write((uint8_t)offset);
    for(uint32_t i=0;i<bytes;i++)dst[i]=spi_read();
    spi_deselect();
}
static uint32_t flash_id(void)
{
    spi_select();spi_write(0x9FU);
    uint32_t a=spi_read(),b=spi_read(),c=spi_read();spi_deselect();
    return (a<<16)|(b<<8)|c;
}
static void ddr_probe(void)
{
    volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)0x00C1F000U;
    for(unsigned pass=0;pass<2;pass++){
        for(unsigned i=0;i<64;i++)p[i]=(0xA5C30000U^(i*0x01010101U))^(pass?UINT32_MAX:0U);
        barrier();
        for(unsigned i=0;i<64;i++)if(p[i]!=((0xA5C30000U^(i*0x01010101U))^(pass?UINT32_MAX:0U)))fatal(0x103U,i);
    }
}
void boot_main(void)
{
    boot_status[0]=0xC0B00001U;
    text("\r\n[BOOT] CGB1 stage2 relocated C00000\r\n");
    ddr_probe();text("[BOOT] DDR probe PASS\r\n");
    spi_init();uint32_t id=flash_id();boot_status[6]=id;
    text("[BOOT] ");field("JEDEC=",id);text("\r\n");
    if((id>>16)==0U||(id>>16)==0xFFU||(id&0xFFU)!=0x17U)fatal(0x104U,id);
    spi_copy(MANIFEST_FLASH_OFFSET,(uint8_t *)&package,sizeof(package));
    int rc=boot_validate(&package,MANIFEST_FLASH_OFFSET);
    if(rc)fatal(0x200U+(uint32_t)rc,MANIFEST_FLASH_OFFSET);
    if(id!=package.jedec_id)fatal(0x105U,id);
    if(REG(0xF8100024U)!=package.gpu_id||REG(0xF8100100U)!=package.bul9_id||
       REG(0xF8100150U)!=package.bul9_revision)fatal(0x2010U,REG(0xF8100150U));
    text("[BOOT] header/compat PASS\r\n");
    /* Replace the accepted host-side display initialization as well as downloads. */
    if(REG(0xF810006CU)!=0x44424731U)fatal(0x106U,REG(0xF810006CU));
    REG(0xF810005CU)=4U;
    __asm__ volatile("fence iorw,iorw":::"memory");
    uint32_t display_start=REG(MTIME_LOW);
    while((uint32_t)(REG(MTIME_LOW)-display_start)<HZ*40U/1000U){}
    uint32_t display_ctrl=REG(0xF810005CU),display_q=REG(0xF8100074U);
    if(display_ctrl!=4U)fatal(0x107U,display_ctrl);
    if(!(display_q&0x400U))fatal(0x108U,display_q);
    text("[BOOT] display relock PASS\r\n");
    /* The validator checks every range before the first low-DDR overwrite. */
    for(unsigned i=0;i<BOOT_SECTIONS;i++){
        const BootSection *s=&package.section[i];boot_status[3]=i+1U;
        text("[BOOT] ");field("section=",i+1U);field(" src=",s->flash_offset);
        field(" dst=",s->ddr_address);field(" bytes=",s->bytes);text("\r\n");
        spi_copy(s->flash_offset,(uint8_t *)(uintptr_t)s->ddr_address,s->bytes);
        barrier();uint32_t crc=boot_crc32((const void *)(uintptr_t)s->ddr_address,s->bytes);
        boot_status[4]=s->crc32;boot_status[5]=crc;
        if(crc!=s->crc32)fatal(0x300U+i,crc);
        text("[BOOT] DDR CRC PASS ");hex(crc);text("\r\n");
    }
    boot_status[1]=0x600DU;boot_status[7]=package.entry;
    text("[BOOT] all sections PASS; jumping 00001000\r\n");
    barrier();__asm__ volatile(".word 0x100F":::"memory");
    ((void (*)(void))(uintptr_t)package.entry)();
    fatal(0x400U,package.entry);
}
