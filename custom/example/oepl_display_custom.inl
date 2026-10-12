
#ifdef HAS_CUSTOM_CONTENT_SPLASH
static void add_rendered_content_splash(void)
{
  uint8_t hwid = oepl_hw_get_hwid();
  uint8_t mac[8];
  oepl_radio_get_mac(mac);
  uint16_t fw = oepl_hw_get_swversion();
  const char* suffix = oepl_hw_get_swsuffix();

  C_epdSetFont(&FreeSansBold18pt7b);
  C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
  C_epdSetFont(&FreeSans9pt7b);
  C_epdPrintf(15, 55, COLOR_RED, ROTATE_0, "Custom BWRY 7.4\", ID: 0x%02X",hwid);
  C_epdPrintf(15, 73, COLOR_YELLOW, ROTATE_0, "Yellow");
  C_epdPrintf(15, xres - 45, COLOR_BLACK, ROTATE_0, "FW: %04X-%s", fw, suffix);
  C_epdPrintf(15, xres - 25, COLOR_BLACK, ROTATE_0,
	      "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
	      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
}
#endif

