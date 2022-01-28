#ifndef RESOURCE_H
#define RESOURCE_H

// Takes a notification code and returns it as an HMENU that uses the high word so it works the same as system notification codes.
#define NOTIF_CODIFY(x) MAKEWPARAM(0, x)

// Nothing special about 101 except that Microsoft uses it in their example so why not.
#define ACCELERATOR_TABLE_ID 101
#define PROGRAM_ICON_ID 102
#define ENABLED_CONTEXT_MENU_ID 103
#define DISABLED_CONTEXT_MENU_ID 104
#define TIME_PICKER_ID 105
#define SECONDS_LISTBOX_ID 106
#define MINUTES_LISTBOX_ID 107
#define HOURS_LISTBOX_ID 108
#define APPLY_PICKER_BTN_ID 109
#define CANCEL_PICKER_BTN_ID 110

// The following are notification codes. Codes below 0x8000 are reserved by Windows.
#define TRAY_ICON_CALLBACK 0x8002
#define PROGRAM_EXIT 0x8003
#define DISABLE_1HR 0x8004
#define DISABLE_2HR 0x8005
#define DISABLE_3HR 0x8006
#define DISABLE_4HR 0x8007
#define DISABLE_CUSTOM 0x8008
#define DISABLE_INDEFINITE 0x8009
#define ENABLE_INDEFINITE 0x800A
#define DISABLE_15MIN 0x800B
#define DISABLE_30MIN 0x800C
#define DISABLE_45MIN 0x800D

#endif
