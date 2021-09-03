#ifndef RESOURCE_H
#define RESOURCE_H

// Takes a notification code and returns it as an HMENU that uses the high word so it works the same as system notification codes.
#define NOTIF_CODIFY(x) MAKEWPARAM(0, x)

// Nothing special about 101 except that Microsoft uses it in their example so why not.
#define ACCELERATOR_TABLE_ID 101
#define PROGRAM_ICON_ID 102
#define CONTEXT_MENU_ID 103

// The following are notification codes. Codes below 0x8000 are reserved by Windows.
#define TOGGLE_ACTIVE 0x8001
#define TRAY_ICON_CALLBACK 0x8002
#define PROGRAM_EXIT 0x8003

#endif
