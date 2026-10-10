/* 
   Driver for PDI 7.4" BWRY EPD (E2741QS0B3)
 
   From the PDI FAQ:
      Q: Where can I find a detailed document on the registers? 
       
      A: Aside from the application notes, we do not share details on the 
      register values to discourage tampering with the embedded values.  This
      is to guarantee optimal functional and optical performance of the EPDs.
      The application notes should suffice when followed. 
    
   Heavy sigh. 
    
   As a result this driver is based on Pervasive's demo code found on github 
   https://github.com/PervasiveDisplays/Pervasive_BWRY_Medium.git
    
   The JD79665AA is the closest controller documentation found for this display.
*/ 
#include "oepl_display_driver.h" 

#include "oepl_display_driver_common.h"
#include "oepl_drawing_capi.h"
#include "sl_udelay.h"

// For debugprint
#include "oepl_hw_abstraction.h"

#include <string.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef E2741QS0B3_DEBUG_PRINT
   #define E2741QS0B3_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if E2741QS0B3_DEBUG_PRINT
   #define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
   #define DEBUG_LOG 1
#else
   #define DPRINTF(...)
#endif

#define OTP_VALID_MAGIC 0xa5
// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void display_init_E2741QS0B3(const oepl_display_parameters_t* display_params);
static void display_draw_E2741QS0B3(void);

static void display_reset(void);
static void display_reinit(void);
static void display_sleep(void);
static void display_refresh_and_wait(void);
static void COG_initial(void);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------
const oepl_display_driver_desc_t oepl_display_driver_E2741QS0B3 =
{
   .init = &display_init_E2741QS0B3,
   .draw = &display_draw_E2741QS0B3
};

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static oepl_display_parameters_t* params = NULL;
static uint8_t gCOG_data[112]; // OTP

static const uint8_t Temp = 0x19;   //	25 C temp - fix me!
static const uint8_t Value00 = 0x00;
static const uint8_t Value01 = 0x01;
static const uint8_t Value02 = 0x02;
static const uint8_t ValueA5 = 0xa5;
static const uint8_t ValueE3 = 0xe3;
static const uint8_t ValueFF = 0xFF;
static const uint8_t COG_temp[3] = {0x07, 0x2b, 0x01};
static const uint8_t COG_temp2[2] = {0xa0, 0x1e};
static const uint8_t COG_temp3[2] = {0x00, 0x00};

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
static void display_init_E2741QS0B3(const oepl_display_parameters_t* display_params) 
{
   DPRINTF("Initialising E2741QS0B3 driver\n");
   oepl_display_driver_common_init();

   if (params == NULL) {
      params = malloc(sizeof(oepl_display_parameters_t));
      if (params == NULL) {
         oepl_hw_crash(DBG_DISPLAY, true, "Can't allocate memory to save display params\n");
      }
   }

// Make local copy since we'll be using most of these
   memcpy(params, display_params, sizeof(oepl_display_parameters_t));
}


/* 
 480 x 800 4 pixels per byte in the X direction
 */
static void display_draw_E2741QS0B3(void) 
{
   DPRINTF("enter E2741QS0B3 draw\n");
   display_reinit();

// BWRY displays need to be fed a single frame with 2 bits per pixel, instead
// of BWR displays which are fed two 1bpp frames. This means we need to render
// the colors for each line and then merge them into a 2bpp encoded line.

   uint8_t* drawline_b = malloc(params->x_res_effective / 8);
   uint8_t* drawline_r = malloc(params->x_res_effective / 8);
   uint8_t* drawline_y = malloc(params->x_res_effective / 8);
   uint8_t* outbuf = malloc(params->x_res_effective / 4);
   if(drawline_b == NULL || drawline_r == NULL || drawline_y == NULL || outbuf == NULL) {
     oepl_hw_crash(DBG_DISPLAY, false, "Out of memory for rendering drawlines");
   }

   COG_initial();
   oepl_display_driver_common_instruction(0x10,false);

   for (uint16_t curY = 0; curY < params->y_res_effective; curY += 1) {
       memset(drawline_b, 0, params->x_res_effective / 8);
       memset(drawline_r, 0, params->x_res_effective / 8);
       memset(drawline_y, 0, params->x_res_effective / 8);

       if (params->mirrorV) {
           C_renderDrawLine(drawline_b, params->y_res_effective - curY - 1, COLOR_BLACK);
           C_renderDrawLine(drawline_r, params->y_res_effective - curY - 1, COLOR_RED);
           C_renderDrawLine(drawline_y, params->y_res_effective - curY - 1, COLOR_YELLOW);
       } else {
           C_renderDrawLine(drawline_b, curY, COLOR_BLACK);
           C_renderDrawLine(drawline_r, curY, COLOR_RED);
           C_renderDrawLine(drawline_y, curY, COLOR_YELLOW);
       }

       for (uint16_t x = 0; x < params->x_res_effective;) {
       // merge color buffers into one
           uint8_t* temp = &(outbuf[x / 4]);
           for (uint8_t shift = 0; shift < 4; shift++) {
               *temp <<= 2;
               uint8_t curByte = x / 8;
               uint8_t curMask = (1 << (7 - (x % 8)));
               if ((drawline_r[curByte] & curMask)) {
                   *temp |= 0x03;
               } else if (drawline_y[curByte] & curMask) {
                   *temp |= 0x02;
               } else if (drawline_b[curByte] & curMask) {
               } else {
                   *temp |= 0x01;
               }
               x++;
           }
       }
    // start transfer of the 2bpp data line
       oepl_display_driver_common_data(
          outbuf,
          params->x_res_effective / 4,false);
   }

   display_refresh_and_wait();
   display_sleep();
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void display_reset(void) 
{
   oepl_display_driver_wait(20);
   DPRINTF("Activating driver\n");
   oepl_display_driver_common_activate();
   DPRINTF("Pulsing reset\n");
// ms_before_assert, ms_to_assert, ms_after_assert
   oepl_display_driver_common_pulse_reset(20,10,20);
// BWRY specific
   oepl_display_driver_wait_busy(0,true);
}

static void display_sleep(void) 
{
   // Application note § 5. Turn-off DC/DC
   // b_sendCommandData8(0x02, 0x00); // Turn off DC/DC

   oepl_display_driver_common_instruction_with_data(
      0x02,&Value00,sizeof(Value00),false);

   oepl_display_driver_wait_busy(0, true);

   // b_sendIndexData(0x00, &COG_temp[0], 3); // PSR
   oepl_display_driver_common_instruction_with_data(
      0x00,COG_temp,sizeof(COG_temp),false);

   // b_sendCommandData8(0xff, 0xa5); // Turn off DC/DC
   oepl_display_driver_common_instruction_with_data(
      0xff,&ValueA5,sizeof(ValueA5),false);

   // hV_HAL_delayMilliseconds(400);
   oepl_display_driver_wait(400);

   // b_sendIndexData(0xee, &COG_temp2[0], 2); // PSR
   oepl_display_driver_common_instruction_with_data(
      0xee,COG_temp2,sizeof(COG_temp2),false);

   // hV_HAL_delayMilliseconds(4);
   oepl_display_driver_wait(4);

   // b_sendIndexData(0xee, &COG_temp3[0], 2); // PSR
   oepl_display_driver_common_instruction_with_data(
      0xee,COG_temp3,sizeof(COG_temp3),false);

   // hV_HAL_delayMilliseconds(3);
   oepl_display_driver_wait(3);

   // b_sendCommandData8(0xff, 0xe3); // Turn off DC/DC
   oepl_display_driver_common_instruction_with_data(
      0xff,&ValueE3,sizeof(ValueE3),false);
   // hV_HAL_delayMilliseconds(6);
// NB: Less than a 500ms delay causes the black levels to degrade slightly
// when power turns off.
   oepl_display_driver_wait(500);

   oepl_display_driver_common_deactivate();
}

struct InitData {
   const uint8_t *Data;
   uint8_t DataLen;
   uint8_t Opcode;
};


static const struct InitData InitData1[] = {
   {&Temp,sizeof(Temp),0xe6},
   {&Value02,sizeof(Value02),0xe0},
   {NULL}   // End of table
};

static uint8_t temphold[3];

static const struct InitData InitData2[] = {
   {&gCOG_data[16],1,0x01},   // PWR
   {&gCOG_data[26],2,0x00},   // PSR
   {&gCOG_data[19],4,0x61},   // TRES
   {&gCOG_data[17],2,0x00},   // PSR
   {&gCOG_data[23],3,0x06},   // BTST_P
   {&gCOG_data[30],3,0x03},   // PFS
   {&gCOG_data[33],1,0xe7},   // PFS
   {&gCOG_data[34],4,0x65},   // TRES
   {&gCOG_data[38],1,0x30},   // PLL
   {&gCOG_data[39],1,0x50},   // CDI
   {&gCOG_data[40],2,0x60},   // TCON
   {&gCOG_data[42],1,0xe3},   // PWS
   {&ValueA5,1,0xff},         // PWS
   {&gCOG_data[43],8,0xef},   // TRES
// CDI
   {&gCOG_data[59],1,0xdc},
   {&gCOG_data[60],1,0xdd},
   {&gCOG_data[61],1,0xde},
   {&gCOG_data[62],1,0xe8},
   {&gCOG_data[63],1,0xda},
   {&ValueE3,1,0xff},
   {&Value01,1,0xe9},
   {NULL}   // End of table
};

static void SendInitValues(const struct InitData *p)
{
   while(p->Data != NULL) {
#if CABLE_AT_BOTTOM
      if(p->Opcode == 00) {
      // PSR, clear the SHL: bit 
         *((uint8_t*) &p->Data[0]) &= ~4;
      }
#endif
      oepl_display_driver_common_instruction_with_data(
         p->Opcode,p->Data,p->DataLen,false);
      p++;
   }
}

static void COG_initial()
{
   temphold[0] = gCOG_data[17];
   temphold[1] = gCOG_data[18];
   temphold[2] = gCOG_data[29];
   SendInitValues(InitData1);
   oepl_display_driver_common_instruction(0xa5,false);
   oepl_display_driver_wait_busy(0, true);
   SendInitValues(InitData2);
}

static void display_refresh_and_wait(void) 
{
   const uint8_t Zero = 0;

// Power on
   oepl_display_driver_common_instruction(0x04,false);
   oepl_display_driver_wait_busy(0, true);

// Display Refresh
   oepl_display_driver_wait_busy(0, true);
   oepl_display_driver_common_instruction_with_data(
      0x12,&Zero,sizeof(Zero),false);

   oepl_display_driver_wait(5);
   oepl_display_driver_wait_busy(0, true);
}

static void ReadOtp(void) 
{
// Reset the display
   display_reset();
   const uint8_t SetAdrArgs[] = {0x00,0x15,0x00,0x00,0xe0};

   for(int Tries = 0; Tries < 2; Tries++) {
      if(Tries == 0) {
      // Read chip ID
         uint16_t ID;
         oepl_display_driver_common_instruction(0x70,false);
         oepl_display_driver_common_dataread(gCOG_data,sizeof(ID),false);
         ID = (gCOG_data[0] << 8) | gCOG_data[1];
         DPRINTF("chip ID 0x%04x\n",ID);
         if(ID != 0x0d04) {
            oepl_hw_crash(DBG_DISPLAY,false,
                          "chip ID error: got 0x%04x expected 0x0d04\n",ID);
            break;
         }
      }
   // read OTP data
      oepl_display_driver_common_instruction(0x90,false);
      oepl_display_driver_wait(8);

      oepl_display_driver_common_instruction_with_data(
         0xa2,SetAdrArgs,sizeof(SetAdrArgs),false);
      oepl_display_driver_wait_busy(0, true);

      oepl_display_driver_common_instruction(0x92,false);
   // read and toss 1 byte
      oepl_display_driver_common_dataread(gCOG_data,1,false);
      oepl_display_driver_common_dataread(gCOG_data,sizeof(gCOG_data),false);
      if(gCOG_data[0] == OTP_VALID_MAGIC) {
         DPRINTF("OTP is valid\n");
         break;
      }
   // page 0 not valid, try page 1
      display_reset();
   } while(false);
}

static void display_reinit(void) 
{
   if(gCOG_data[0] != OTP_VALID_MAGIC) {
      ReadOtp();
   }

   if(gCOG_data[0] != OTP_VALID_MAGIC) {
      oepl_hw_crash(DBG_DISPLAY, true, "Couldn't read OTP data\n");
   }

// Reset the display
   display_reset();
}

