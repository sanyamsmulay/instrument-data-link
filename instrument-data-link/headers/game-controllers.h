#ifndef _WIN32
// Windows-specific types needed for cross-platform compilation
typedef unsigned long DWORD;
typedef unsigned long MMRESULT;
#define JOYERR_NOERROR 0

typedef struct {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwXpos;
    DWORD dwYpos;
    DWORD dwZpos;
    DWORD dwRpos;
    DWORD dwUpos;
    DWORD dwVpos;
    DWORD dwButtons;
    DWORD dwButtonNumber;
    DWORD dwPOV;
    DWORD dwReserved1;
    DWORD dwReserved2;
} JOYINFOEX, *LPJOYINFOEX;
#endif

const int MaxJoysticks = 16;
const int MaxAxes = 6;
const int MaxButtons = 32;

struct Joystick
{
	int mid;
	int pid;
	char name[32];
	int axisCount;
	int buttonCount;
	bool initialised;
	bool zeroed;
	int axisZero[MaxAxes];
	int axis[MaxAxes];
	int button[MaxButtons];
};

void initJoysticks();
void refreshJoysticks();
void joyRefresh(int id);
