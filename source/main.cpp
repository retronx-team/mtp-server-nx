#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <thread>

#include "SwitchMtpDatabase.h"
#include "MtpServer.h"
#include "MtpStorage.h"

#include "log.h"

using namespace android;

static void stop_thread(MtpServer* server)
{
    while (appletMainLoop())
    {
        hidScanInput();
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS)
        {
            server->stop();
            break;
        }
    }
}

int main(int argc, char* argv[])
{
    int c;
    struct option long_options[] =
    {
      {"nxlink",  no_argument,       &nxlink, 1},
      {"verbose", required_argument, 0, 'v'},
      {0, 0, 0, 0}
    };

    while(1)
    {
        int option_index = 0;
        c = getopt_long (argc, argv, "v:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {
            case 'v':
              verbose_level = atoi(optarg);
              break;
            default:
              break;
        }
    }

    consoleInit(NULL);
    std::cout << "Press + to exit";

    struct usb_device_descriptor device_descriptor = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0110,
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,
        .bMaxPacketSize0 = 0x40,
        .idVendor = 0x057e,
        .idProduct = 0x4000,
        .bcdDevice = 0x0100,
        .bNumConfigurations = 0x01
    };

    UsbInterfaceDesc infos[2];
    int num_interface = 0;
    USBMtpInterface *mtp_interface = NULL;
    USBSerialInterface *serial_interface = NULL;

    mtp_interface = new USBMtpInterface(num_interface, &infos[num_interface]);
    num_interface++;

    if(nxlink)
    {
      serial_interface = new USBSerialInterface(num_interface, &infos[num_interface]);
      num_interface++;
    }

    usbInitialize(&device_descriptor, num_interface, infos);
    nxlinkStdioInitialise(serial_interface);

    MtpStorage* storage = new MtpStorage(
      MTP_STORAGE_REMOVABLE_RAM,
      "sdmc:/",
      "sdcard",
      1024U * 1024U * 100U,  /* 100 MB reserved space, to avoid filling the disk */
      false,
      1024U * 1024U * 1024U * 2U  /* 2GB arbitrary max file size */);

    MtpDatabase* mtp_database = new SwitchMtpDatabase();

    mtp_database->addStoragePath("sdmc:/",
                                 "sdcard",
                                 MTP_STORAGE_REMOVABLE_RAM, true);

    MtpServer* server = new MtpServer(
      mtp_interface,
      mtp_database,
      false,
      0,
      0,
      0);
  
    std::thread th(stop_thread, server); 
    server->addStorage(storage);
    server->run();
    th.join();

    nxlinkStdioClose(serial_interface);
    consoleExit(NULL);
    usbExit();
    return 0;
}
