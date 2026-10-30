/*
 * ========================================================================
 * PROMPRO - EPROM Data Reader with Hex Editor
 * ========================================================================
 *
 * Developer: Mickey W. Lawless
 * Date Completed: December 21, 2025
 * Version: 1.6 (one source: Win32 console, macOS/Linux/BSD terminal,
 *               16-bit MS-DOS / Turbo C)
 *
 * Description:
 *   Professional EPROM data reader and hex editor.
 *   Reads ASCII space-separated hex data from a serial port and
 *   provides a full-featured hex editor, file load/save and upload
 *   to EPROM programmers.
 *
 * Features:
 *   - Serial port with configurable baud rates
 *       Windows: COM1..COM255        DOS: COM1..COM4 (interrupt driven)
 *       macOS/Linux: any tty device (/dev/cu.*, /dev/ttyUSB*, ...)
 *   - 64KB buffer capacity
 *   - Full hex/ASCII editing with visual highlighting
 *   - File load/save operations (binary, Intel HEX, listings)
 *   - Upload to EPROM programmer via the serial port
 *   - Print support (Windows/DOS: LPT1, macOS/Linux: lpr)
 *   - Command-line file loading
 *   - ALT-key command help menu (macOS/Linux: also F2..F8)
 *   - Resizable text window; the UI follows the window size
 *
 * Building:
 *   Windows  : gcc/MinGW-w64 or MSVC, link winmm (see prompro.rc for the
 *              icon and version resource):
 *                gcc -O2 -o PROMPRO.EXE prompro.c prompro_res.o -lwinmm
 *   macOS/Unix: cc -O2 -o prompro prompro.c        (or: make)
 *   MS-DOS   : Turbo C++ 1.0 or later, medium model. The program is too
 *              big for one 64KB code segment, so it is compiled as four
 *              modules from this one file - run BUILD.BAT.
 *
 * Change log:
 *   v1.6 - One source for three platforms. New: macOS/Linux/BSD terminal
 *          version (ANSI terminal output, termios serial, /dev port
 *          selection, settings in ~/.prompro) and a 16-bit MS-DOS
 *          Turbo C version (conio, 8250/16550 interrupt-driven COM1-4,
 *          huge-pointer 64KB buffer, 43/50-line mode under F11).
 *          Fixed: 64KB listing/print loops used a 16-bit counter.
 *   v1.5 - Resizable window: the console is no longer locked to 80x25.
 *          The whole UI (menu bar, hex grid, status/hint/footer rows,
 *          dialogs) is laid out from the live window size and redraws
 *          when the window is resized. Wider windows show 32/48/64
 *          bytes per line, taller windows show more rows. F11 or
 *          Options > Window Size sets/maximizes the window.
 *   v1.5 - Ported to Win32 console. conio/BIOS/DOS calls replaced by a
 *          Win32 console layer; COM1 8250 port I/O + IRQ4 ISR replaced by
 *          the Windows comm API (the 8250 no-FIFO limitation no longer
 *          applies - the driver buffers receive data). COM port selectable
 *          (Options > COM Port, /COMn). Long file names in the file browser.
 *          Config files stored next to the EXE.
 *   v1.3 - Fixed premature EOF: timeout now counts only dead-idle iterations
 *        - Added SendInitString()
 *        - Added 16 EZ-WRITER upload formats (Codes 20,10,11,82,50,51,56,
 *          80,81,83,85,86,87,88,95 plus raw binary)
 *   v1.2 - farmalloc() used for DataBuffer to support 64KB chips
 *
 * Copyright (c) 2025 Mickey W. Lawless
 * All Rights Reserved
 * ========================================================================
 */

/* ---- platform selection ---------------------------------------------------
   PP_WIN32 / PP_DOS / PP_POSIX - exactly one is 1.
   PP_PART  - DOS only: 1..4 compiles one quarter of the program (see
              BUILD.BAT); 0 = everything in one module.                     */
#if defined(_WIN32)
#  define PP_WIN32 1
#  define PP_DOS   0
#  define PP_POSIX 0
#elif defined(__MSDOS__) || defined(__TURBOC__)
#  define PP_WIN32 0
#  define PP_DOS   1
#  define PP_POSIX 0
#else
#  define PP_WIN32 0
#  define PP_DOS   0
#  define PP_POSIX 1
#endif
#ifndef PP_PART
#  define PP_PART 0
#endif
#define PP_IN(n) (PP_PART == 0 || PP_PART == (n))

#if PP_PART > 1
#  define PP_V(d)     extern d
#  define PP_VI(d, i) extern d
#else
#  define PP_V(d)     d
#  define PP_VI(d, i) d = i
#endif

#if PP_WIN32
#  ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mmsystem.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <stdarg.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "winmm.lib")
#    pragma comment(lib, "advapi32.lib")
#  endif
#  define PP_MAXCOM       255
#  define PP_PORTNAME     "COM Port"
#  define PP_STRICMP      _stricmp
#  define PP_STRNICMP     _strnicmp
#  define PP_PRINTER      "LPT1"
#  define PP_BUFPTR       unsigned char *
#  define PP_ALLOC(n)     ((unsigned char *)malloc((size_t)(n)))
#  define PP_FREE(p)      free(p)
#  define Plat_AltHeld()  ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0)
#elif PP_DOS
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <stdarg.h>
#  include <ctype.h>
#  include <conio.h>
#  include <dos.h>
#  include <dir.h>
#  include <alloc.h>
#  include <bios.h>
#  define MAX_PATH        MAXPATH
#  define PP_MAXCOM       4
#  define PP_PORTNAME     "COM Port"
#  define PP_STRICMP      stricmp
#  define PP_STRNICMP     strnicmp
#  define PP_PRINTER      "LPT1"
#  define PP_BUFPTR       unsigned char huge *
#  define PP_ALLOC(n)     ((unsigned char huge *)farmalloc((unsigned long)(n)))
#  define PP_FREE(p)      farfree((void far *)(p))
#  define Plat_AltHeld()  ((peekb(0x40, 0x17) & 8) != 0)
#else
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#  endif
#  ifndef _DARWIN_C_SOURCE
#    define _DARWIN_C_SOURCE
#  endif
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <stdarg.h>
#  include <ctype.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <signal.h>
#  include <time.h>
#  include <dirent.h>
#  include <termios.h>
#  include <strings.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#  include <sys/time.h>
#  ifdef __linux__
#    include <linux/serial.h>
#  endif
#  define MAX_PATH        512
#  define PP_PORTNAME     "Serial Port"
#  define PP_STRICMP      strcasecmp
#  define PP_STRNICMP     strncasecmp
#  define PP_PRINTER      "printer (lpr)"
#  define PP_BUFPTR       unsigned char *
#  define PP_ALLOC(n)     ((unsigned char *)malloc((size_t)(n)))
#  define PP_FREE(p)      free(p)
#  define Plat_AltHeld()  1      /* no modifier polling: hint bar stays on */
#endif

/* ---- colours / conio constants (the program is written against conio) ----- */
#if !PP_DOS
#define BLACK        0
#define BLUE         1
#define GREEN        2
#define CYAN         3
#define RED          4
#define MAGENTA      5
#define BROWN        6
#define LIGHTGRAY    7
#define DARKGRAY     8
#define LIGHTBLUE    9
#define LIGHTGREEN   10
#define LIGHTCYAN    11
#define LIGHTRED     12
#define LIGHTMAGENTA 13
#define YELLOW       14
#define WHITE        15

#define C80            3
#define _NOCURSOR      0
#define _SOLIDCURSOR   1
#define _NORMALCURSOR  2
#endif

#define CON_MINW 80
#define CON_MINH 25
#define CON_MAXW 300
#define CON_MAXH 120
#define SCAN_F11    0x85   /* BIOS-style scan code for F11 */
#define SCAN_RESIZE 0xFE   /* pseudo extended key: window size changed */

PP_VI(int ConW, 80);       /* live text-window size the UI is laid out for */
PP_VI(int ConH, 25);
PP_VI(int conResized, 0);  /* set when the window size changed */
PP_VI(int conMaxed, 0);    /* F11 / Window Size: maximized state */
#define CON_W ConW
#define CON_H ConH

void CloseCOM1(void);
unsigned long GetMsTicks(void);
void ConSetWindow(int w, int h);
int  ConSyncSize(void);
void ConToggleMax(void);

#if !PP_DOS
/* Alt+letter -> BIOS scan code (set 1), indexed by letter */
static const unsigned char ConAltScan[26] = {
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
    0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
};
#endif

#if PP_WIN32
#ifndef ERROR_DEVICE_DOES_NOT_EXIST
#define ERROR_DEVICE_DOES_NOT_EXIST 433
#endif
/* ========================================================================
   WIN32 CONSOLE LAYER
   ------------------------------------------------------------------------
   Provides the Borland conio calls this program was written against
   (textcolor, textbackground, gotoxy, cprintf, clrscr, clreol, getch,
   kbhit, delay, textmode, _setcursortype) on top of the Win32 console.
   Output goes to an 80x25 shadow buffer that is pushed to the screen with
   one WriteConsoleOutputW call (CP437 glyphs are mapped to Unicode, so
   the box-drawing / block characters render regardless of the console
   code page). Keyboard input is read as console key events and translated
   to the DOS convention: 0 followed by a BIOS scan code for extended keys,
   Alt+letter = 0 + the set-1 scan code of that letter.
   ======================================================================== */

static HANDLE    hConOut = INVALID_HANDLE_VALUE;
static HANDLE    hConIn  = INVALID_HANDLE_VALUE;
static DWORD     conOldInMode = 0;
static int       conHaveOldMode = 0;
static CHAR_INFO conCells[CON_MAXH][CON_MAXW];
static WORD      cp437tab[256];
static int       conX = 0, conY = 0;
static WORD      conAttr = 7;
static int       conCursorOn = 1;
static int       conDirty = 0;
static int       conPendingKey = -1;
static DWORD     conLastFlush = 0;


static void ConGetOrigin(int *ox, int *oy) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    *ox = 0; *oy = 0;
    if (GetConsoleScreenBufferInfo(hConOut, &csbi)) {
        *ox = csbi.srWindow.Left;
        *oy = csbi.srWindow.Top;
    }
}

void ConFlush(void) {
    COORD sz, org, cp;
    SMALL_RECT r;
    int ox, oy;
    if (!conDirty) return;
    ConGetOrigin(&ox, &oy);
    sz.X = CON_MAXW; sz.Y = CON_MAXH;
    org.X = 0;    org.Y = 0;
    r.Left   = (SHORT)ox;
    r.Top    = (SHORT)oy;
    r.Right  = (SHORT)(ox + CON_W - 1);
    r.Bottom = (SHORT)(oy + CON_H - 1);
    WriteConsoleOutputW(hConOut, &conCells[0][0], sz, org, &r);
    cp.X = (SHORT)(ox + conX);
    cp.Y = (SHORT)(oy + conY);
    SetConsoleCursorPosition(hConOut, cp);
    conDirty = 0;
    conLastFlush = GetTickCount();
}

static void ConScroll(void) {
    int x, y;
    for (y = 0; y < CON_H - 1; y++)
        memmove(&conCells[y][0], &conCells[y + 1][0], sizeof(CHAR_INFO) * CON_W);
    for (x = 0; x < CON_W; x++) {
        conCells[CON_H - 1][x].Char.UnicodeChar = L' ';
        conCells[CON_H - 1][x].Attributes = conAttr;
    }
}

static void ConPutC(int c) {
    if (c == '\r') { conX = 0; return; }
    if (c == '\n') {
        conX = 0; conY++;
    } else if (c == '\b') {
        if (conX > 0) conX--;
    } else {
        conCells[conY][conX].Char.UnicodeChar = cp437tab[c & 0xFF];
        conCells[conY][conX].Attributes = conAttr;
        conX++;
        if (conX >= CON_W) { conX = 0; conY++; }
    }
    if (conY >= CON_H) { ConScroll(); conY = CON_H - 1; }
    conDirty = 1;
}

int cprintf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    int n, i;
    va_start(ap, fmt);
    n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    buf[n] = '\0';
    for (i = 0; i < n; i++) ConPutC((unsigned char)buf[i]);
    return n;
}

void textcolor(int c)      { conAttr = (WORD)((conAttr & 0xF0) | (c & 0x0F)); }
void textbackground(int c) { conAttr = (WORD)((conAttr & 0x0F) | ((c & 0x07) << 4)); }

void gotoxy(int x, int y) {
    if (x < 1) x = 1;
    if (x > CON_W) x = CON_W;
    if (y < 1) y = 1;
    if (y > CON_H) y = CON_H;
    conX = x - 1;
    conY = y - 1;
    conDirty = 1;
}

void clrscr(void) {
    int x, y;
    for (y = 0; y < CON_H; y++)
        for (x = 0; x < CON_W; x++) {
            conCells[y][x].Char.UnicodeChar = L' ';
            conCells[y][x].Attributes = conAttr;
        }
    conX = 0; conY = 0;
    conDirty = 1;
}

void clreol(void) {
    int x;
    for (x = conX; x < CON_W; x++) {
        conCells[conY][x].Char.UnicodeChar = L' ';
        conCells[conY][x].Attributes = conAttr;
    }
    conDirty = 1;
}

void textmode(int mode) {
    (void)mode;
    conAttr = 7;
    clrscr();
}

void _setcursortype(int t) {
    CONSOLE_CURSOR_INFO ci;
    conCursorOn = (t != _NOCURSOR);
    ci.dwSize = (t == _SOLIDCURSOR) ? 100 : 25;
    ci.bVisible = conCursorOn ? TRUE : FALSE;
    SetConsoleCursorInfo(hConOut, &ci);
}

void delay(unsigned int ms) {
    ConFlush();
    Sleep(ms);
}

/* ---- window size handling ------------------------------------------------
   ConSetWindow  - ask the console host for a w x h window (clamped to the
                   largest window the screen allows). The screen buffer is
                   only ever grown, never shrunk, so the window can still be
                   dragged larger by hand afterwards.
   ConSyncSize   - adopt the console's current window size as the UI size
                   (80x25 minimum). Returns 1 if the UI size changed.
   ConToggleMax  - F11: maximize, or restore the previous size.            */
static int conSaveW = 80, conSaveH = 25;

void ConSetWindow(int w, int h) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD sz, largest;
    SMALL_RECT wr;

    largest = GetLargestConsoleWindowSize(hConOut);
    if (largest.X > 0 && w > largest.X) w = largest.X;
    if (largest.Y > 0 && h > largest.Y) h = largest.Y;
    if (w > CON_MAXW) w = CON_MAXW;
    if (h > CON_MAXH) h = CON_MAXH;
    if (w < CON_MINW) w = CON_MINW;
    if (h < CON_MINH) h = CON_MINH;
    if (!GetConsoleScreenBufferInfo(hConOut, &csbi)) return;
    sz = csbi.dwSize;
    if (sz.X < w) sz.X = (SHORT)w;
    if (sz.Y < h) sz.Y = (SHORT)h;
    if (sz.X != csbi.dwSize.X || sz.Y != csbi.dwSize.Y)
        SetConsoleScreenBufferSize(hConOut, sz);
    wr.Left = 0; wr.Top = 0; wr.Right = (SHORT)(w - 1); wr.Bottom = (SHORT)(h - 1);
    SetConsoleWindowInfo(hConOut, TRUE, &wr);
}

int ConSyncSize(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int w, h, x, y, ow = ConW, oh = ConH;

    if (!GetConsoleScreenBufferInfo(hConOut, &csbi)) return 0;
    w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if (w < CON_MINW || h < CON_MINH) {          /* too small for the UI: grow */
        ConSetWindow(w < CON_MINW ? CON_MINW : w, h < CON_MINH ? CON_MINH : h);
        if (GetConsoleScreenBufferInfo(hConOut, &csbi)) {
            w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        }
    }
    if (w > CON_MAXW) w = CON_MAXW;
    if (h > CON_MAXH) h = CON_MAXH;
    if (w < CON_MINW) w = CON_MINW;
    if (h < CON_MINH) h = CON_MINH;
    if (w == ConW && h == ConH) return 0;

    ConW = w; ConH = h;
    for (y = 0; y < ConH; y++)
        for (x = 0; x < ConW; x++)
            if (y >= oh || x >= ow) {
                conCells[y][x].Char.UnicodeChar = L' ';
                conCells[y][x].Attributes = 7;
            }
    if (conX >= ConW) conX = ConW - 1;
    if (conY >= ConH) conY = ConH - 1;
    conDirty = 1;
    conResized = 1;
    return 1;
}

void ConToggleMax(void) {
    if (!conMaxed) {
        conSaveW = ConW; conSaveH = ConH;
        ConSetWindow(CON_MAXW, CON_MAXH);
        conMaxed = 1;
    } else {
        ConSetWindow(conSaveW, conSaveH);
        conMaxed = 0;
    }
    ConSyncSize();
}

/* Returns the first byte of a key (0 = extended, scan code in *second), or
   -1 if the record is not a key press this program cares about. */
static int ConTranslate(const INPUT_RECORD *rec, int *second) {
    WORD vk;
    DWORD ctl;
    unsigned char ch;

    *second = -1;
    if (rec->EventType != KEY_EVENT || !rec->Event.KeyEvent.bKeyDown) return -1;
    vk  = rec->Event.KeyEvent.wVirtualKeyCode;
    ctl = rec->Event.KeyEvent.dwControlKeyState;
    ch  = (unsigned char)rec->Event.KeyEvent.uChar.AsciiChar;

    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
        vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) return -1;

    if ((ctl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) &&
        !(ctl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
        if (vk >= 'A' && vk <= 'Z') { *second = ConAltScan[vk - 'A']; return 0; }
        return -1;
    }

    switch (vk) {
        case VK_UP:     *second = 72;   return 0;
        case VK_DOWN:   *second = 80;   return 0;
        case VK_LEFT:   *second = 75;   return 0;
        case VK_RIGHT:  *second = 77;   return 0;
        case VK_PRIOR:  *second = 73;   return 0;
        case VK_NEXT:   *second = 81;   return 0;
        case VK_HOME:   *second = 71;   return 0;
        case VK_END:    *second = 79;   return 0;
        case VK_INSERT: *second = 82;   return 0;
        case VK_DELETE: *second = 83;   return 0;
        case VK_F1:     *second = 0x3B; return 0;
        case VK_F11:    *second = SCAN_F11; return 0;
    }
    if (ch != 0) return ch;
    return -1;
}

int kbhit(void) {
    INPUT_RECORD rec;
    DWORD n;
    int second;
    static DWORD conLastPoll = 0;
    if (conPendingKey >= 0) return 1;
    /* The console host does not send an event when only the window height
       changes (buffer unchanged), so poll the window size ~10x/second. */
    if ((GetTickCount() - conLastPoll) >= 100) { conLastPoll = GetTickCount(); ConSyncSize(); }
    if (conDirty && (GetTickCount() - conLastFlush) >= 25) ConFlush();
    for (;;) {
        if (!PeekConsoleInputA(hConIn, &rec, 1, &n) || n == 0) return 0;
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            ConSyncSize();
            ReadConsoleInputA(hConIn, &rec, 1, &n);
            continue;
        }
        if (ConTranslate(&rec, &second) >= 0) return 1;
        ReadConsoleInputA(hConIn, &rec, 1, &n);
    }
}

int getch(void) {
    INPUT_RECORD rec;
    DWORD n;
    int first, second;
    if (conPendingKey >= 0) {
        first = conPendingKey;
        conPendingKey = -1;
        return first;
    }
    ConFlush();
    for (;;) {
        if (WaitForSingleObject(hConIn, 100) == WAIT_TIMEOUT) {   /* idle: watch for window resize */
            if (ConSyncSize()) { conPendingKey = SCAN_RESIZE; return 0; }
            continue;
        }
        if (!ReadConsoleInputA(hConIn, &rec, 1, &n)) return 27;
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            if (ConSyncSize()) { conPendingKey = SCAN_RESIZE; return 0; }
            continue;
        }
        first = ConTranslate(&rec, &second);
        if (first < 0) continue;
        if (first == 0 && second == SCAN_F11) { ConToggleMax(); second = SCAN_RESIZE; }
        if (first == 0) conPendingKey = second;
        return first;
    }
}

static void ConShutdown(void) {
    COORD cp;
    CONSOLE_CURSOR_INFO ci;
    int ox, oy, row;
    CloseCOM1();
    conDirty = 1;
    ConFlush();
    ConGetOrigin(&ox, &oy);
    row = conY + 1;
    if (row >= CON_H) row = CON_H - 1;
    cp.X = (SHORT)ox;
    cp.Y = (SHORT)(oy + row);
    SetConsoleCursorPosition(hConOut, cp);
    ci.dwSize = 25;
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hConOut, &ci);
    if (conHaveOldMode) SetConsoleMode(hConIn, conOldInMode);
    timeEndPeriod(1);
}

void ConInit(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD at;
    DWORD mode, written;
    int i, row, wcols, wrows;
    unsigned char c;
    WORD w;

    hConOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hConIn  = GetStdHandle(STD_INPUT_HANDLE);

    for (i = 0; i < 256; i++) {
        c = (unsigned char)i;
        w = (WORD)i;
        MultiByteToWideChar(437, 0, (LPCSTR)&c, 1, (LPWSTR)&w, 1);
        cp437tab[i] = w;
    }
    cp437tab[0x10] = 0x25BA;  cp437tab[0x11] = 0x25C4;
    cp437tab[0xB0] = 0x2591;  cp437tab[0xB1] = 0x2592;  cp437tab[0xB2] = 0x2593;
    cp437tab[0xDB] = 0x2588;  cp437tab[0xC4] = 0x2500;  cp437tab[0xB3] = 0x2502;
    cp437tab[0xDA] = 0x250C;  cp437tab[0xBF] = 0x2510;  cp437tab[0xC0] = 0x2514;
    cp437tab[0xD9] = 0x2518;  cp437tab[0xC3] = 0x251C;  cp437tab[0xB4] = 0x2524;

    /* Adopt the console's current window size (grown to 80x25 if smaller).
       The window is no longer locked, so it can be resized freely. */
    ConSyncSize();
    /* Blank the whole visible window so nothing from the shell shows
       around the 80x25 UI. */
    if (GetConsoleScreenBufferInfo(hConOut, &csbi)) {
        wcols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        wrows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        for (row = 0; row < wrows; row++) {
            at.X = csbi.srWindow.Left;
            at.Y = (SHORT)(csbi.srWindow.Top + row);
            FillConsoleOutputCharacterA(hConOut, ' ', (DWORD)wcols, at, &written);
            FillConsoleOutputAttribute(hConOut, 7, (DWORD)wcols, at, &written);
        }
    }

    /* Raw key events; quick-edit off so the mouse can't freeze the console. */
    if (GetConsoleMode(hConIn, &mode)) {
        conOldInMode = mode;
        conHaveOldMode = 1;
        SetConsoleMode(hConIn, ENABLE_EXTENDED_FLAGS | ENABLE_PROCESSED_INPUT);
    }
    FlushConsoleInputBuffer(hConIn);
    SetConsoleTitleA("EPROM Data Reader & Hex Editor");

    /* 1 ms timer resolution so delay(1)/delay(2) pacing is real. */
    timeBeginPeriod(1);
    atexit(ConShutdown);

    conAttr = 7;
    clrscr();
    _setcursortype(_NORMALCURSOR);
    ConFlush();
}
#define ConExitScreen() (textmode(C80), clrscr())
#endif /* PP_WIN32 */

#if PP_POSIX
/* ========================================================================
   POSIX (macOS / Linux / BSD) CONSOLE LAYER
   ------------------------------------------------------------------------
   Provides the same Borland-conio-style calls on top of an ANSI/VT100
   terminal (Terminal.app, iTerm2, xterm, GNOME Terminal, ...). Output is
   drawn into a shadow buffer; ConFlush() sends only the cells that changed
   (CP437 glyphs are sent as UTF-8). Keys are decoded from the terminal's
   escape sequences into the DOS convention: 0 followed by a BIOS scan
   code for extended keys, Alt+letter = 0 + the set-1 scan code of the
   letter.

   Alt+letter reaches the program as ESC+letter when the terminal is set to
   "use Option as Meta" (Terminal.app: Settings > Profiles > Keyboard).
   Without that, the Option combinations that produce a letter-like glyph
   on a US Mac keyboard are recognised too, and F2..F8 always open the
   menus (Setup, Data, Program, Tasks, Utilities, Options, Exit).
   ======================================================================== */
typedef struct { unsigned short ch; unsigned short at; } ConCell;

static ConCell   conCells[CON_MAXH][CON_MAXW];
static ConCell   conShown[CON_MAXH][CON_MAXW];
static int       conX = 0, conY = 0;
static int       conAttr = 7;
static int       conCursorOn = 1;
static int       conDirty = 0;
static int       conForce = 1;
static int       conPlain = 0;       /* screen holds only plain cprintf text */
static int       conActive = 0;
static int       conPendingKey = -1;
static int       conStash = -1, conStashSecond = -1;
static unsigned long conLastFlush = 0;
static unsigned long conLastPoll = 0;
static struct termios conOldTio;
static int       conHaveTio = 0;
static volatile sig_atomic_t conWinch = 0;
static unsigned char conInBuf[128];
static int       conInLen = 0, conInPos = 0;
static char     *conOut = NULL;
static size_t    conOutLen = 0, conOutCap = 0;
static int       conSaveW = 80, conSaveH = 25;

static const unsigned short cp437tab[256] = {
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x25BA, 0x25C4, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x0020,
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

/* DOS colour number -> ANSI colour number (DOS: blue=1, ANSI: red=1) */
static const unsigned char conAnsi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

unsigned long GetMsTicks(void);

static void ConOutRaw(const char *s, size_t n) {
    if (conOutLen + n > conOutCap) {
        size_t nc = conOutCap ? conOutCap : 8192;
        while (nc < conOutLen + n) nc *= 2;
        conOut = (char *)realloc(conOut, nc);
        if (conOut == NULL) { conOutCap = 0; conOutLen = 0; return; }
        conOutCap = nc;
    }
    memcpy(conOut + conOutLen, s, n);
    conOutLen += n;
}

static void ConOutStr(const char *s) { ConOutRaw(s, strlen(s)); }

static void ConOutWrite(void) {
    size_t off = 0;
    while (off < conOutLen) {
        ssize_t w = write(STDOUT_FILENO, conOut + off, conOutLen - off);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        off += (size_t)w;
    }
    conOutLen = 0;
}

static int ConUtf8(unsigned cp, char *o) {
    if (cp < 0x80)  { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | (cp >> 12));
    o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

void ConFlush(void) {
    int x, y, x0, x1;
    unsigned curAt = 0xFFFFu;
    char tmp[48], u[4];
    int n;

    if (!conDirty || !conActive) return;
    for (y = 0; y < ConH; y++) {
        x0 = 0; x1 = ConW - 1;
        if (!conForce) {
            while (x0 <= x1 && memcmp(&conCells[y][x0], &conShown[y][x0], sizeof(ConCell)) == 0) x0++;
            if (x0 > x1) continue;
            while (memcmp(&conCells[y][x1], &conShown[y][x1], sizeof(ConCell)) == 0) x1--;
        }
        n = sprintf(tmp, "\033[%d;%dH", y + 1, x0 + 1);
        ConOutRaw(tmp, (size_t)n);
        for (x = x0; x <= x1; x++) {
            unsigned at = conCells[y][x].at;
            if (at != curAt) {
                int f = at & 15, b = (at >> 4) & 7;
                n = sprintf(tmp, "\033[%d;%dm",
                            (f < 8) ? 30 + conAnsi[f] : 90 + conAnsi[f - 8],
                            40 + conAnsi[b]);
                ConOutRaw(tmp, (size_t)n);
                curAt = at;
            }
            n = ConUtf8(conCells[y][x].ch, u);
            ConOutRaw(u, (size_t)n);
        }
        memcpy(&conShown[y][x0], &conCells[y][x0], sizeof(ConCell) * (size_t)(x1 - x0 + 1));
    }
    conForce = 0;
    n = sprintf(tmp, "\033[%d;%dH%s", conY + 1, conX + 1, conCursorOn ? "\033[?25h" : "\033[?25l");
    ConOutRaw(tmp, (size_t)n);
    ConOutWrite();
    conDirty = 0;
    conLastFlush = GetMsTicks();
}

static void ConScroll(void) {
    int x, y;
    for (y = 0; y < ConH - 1; y++)
        memmove(&conCells[y][0], &conCells[y + 1][0], sizeof(ConCell) * (size_t)ConW);
    for (x = 0; x < ConW; x++) {
        conCells[ConH - 1][x].ch = ' ';
        conCells[ConH - 1][x].at = (unsigned short)conAttr;
    }
}

static void ConPutC(int c) {
    if (c == '\r') { conX = 0; return; }
    if (c == '\n') {
        conX = 0; conY++;
    } else if (c == '\b') {
        if (conX > 0) conX--;
    } else {
        conCells[conY][conX].ch = cp437tab[c & 0xFF];
        conCells[conY][conX].at = (unsigned short)conAttr;
        conX++;
        if (conX >= ConW) { conX = 0; conY++; }
    }
    if (conY >= ConH) { ConScroll(); conY = ConH - 1; }
    conDirty = 1;
}

int cprintf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    int n, i;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    for (i = 0; i < n; i++) ConPutC((unsigned char)buf[i]);
    return n;
}

void textcolor(int c)      { conAttr = (conAttr & 0xF0) | (c & 0x0F); }
void textbackground(int c) { conAttr = (conAttr & 0x0F) | ((c & 0x07) << 4); }

void gotoxy(int x, int y) {
    if (x < 1) x = 1;
    if (x > ConW) x = ConW;
    if (y < 1) y = 1;
    if (y > ConH) y = ConH;
    conX = x - 1;
    conY = y - 1;
    conDirty = 1;
    conPlain = 0;
}

void clrscr(void) {
    int x, y;
    for (y = 0; y < ConH; y++)
        for (x = 0; x < ConW; x++) {
            conCells[y][x].ch = ' ';
            conCells[y][x].at = (unsigned short)conAttr;
        }
    conX = 0; conY = 0;
    conDirty = 1;
    conPlain = 1;
}

void clreol(void) {
    int x;
    for (x = conX; x < ConW; x++) {
        conCells[conY][x].ch = ' ';
        conCells[conY][x].at = (unsigned short)conAttr;
    }
    conDirty = 1;
    conPlain = 0;
}

void textmode(int mode) {
    (void)mode;
    conAttr = 7;
    clrscr();
}

void _setcursortype(int t) {
    conCursorOn = (t != _NOCURSOR);
    conDirty = 1;
}

void delay(unsigned int ms) {
    struct timespec ts;
    ConFlush();
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

/* ---- window size handling ------------------------------------------------
   The terminal owns its window, so the UI simply follows it (SIGWINCH).
   ConSetWindow / ConToggleMax ask the terminal emulator to resize with the
   xterm window-manipulation sequences; terminals that ignore them keep
   working, the user just resizes by hand.                                  */
int ConSyncSize(void) {
    struct winsize ws;
    int w = ConW, h = ConH, x, y, ow = ConW, oh = ConH;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = ws.ws_col;
        h = ws.ws_row;
    }
    if (w > CON_MAXW) w = CON_MAXW;
    if (h > CON_MAXH) h = CON_MAXH;
    if (w < CON_MINW) w = CON_MINW;
    if (h < CON_MINH) h = CON_MINH;
    if (w == ConW && h == ConH) return 0;

    ConW = w; ConH = h;
    for (y = 0; y < ConH; y++)
        for (x = 0; x < ConW; x++)
            if (y >= oh || x >= ow) {
                conCells[y][x].ch = ' ';
                conCells[y][x].at = 7;
            }
    if (conX >= ConW) conX = ConW - 1;
    if (conY >= ConH) conY = ConH - 1;
    conForce = 1;
    conDirty = 1;
    conResized = 1;
    return 1;
}

void ConSetWindow(int w, int h) {
    char tmp[40];
    if (w > CON_MAXW) w = CON_MAXW;
    if (h > CON_MAXH) h = CON_MAXH;
    if (w < CON_MINW) w = CON_MINW;
    if (h < CON_MINH) h = CON_MINH;
    sprintf(tmp, "\033[8;%d;%dt", h, w);
    ConOutStr(tmp);
    ConOutWrite();
}

void ConToggleMax(void) {
    if (!conMaxed) {
        conSaveW = ConW; conSaveH = ConH;
        ConOutStr("\033[9;1t");
        conMaxed = 1;
    } else {
        ConOutStr("\033[9;0t");
        conMaxed = 0;
    }
    ConOutWrite();
}

/* ---- keyboard ------------------------------------------------------------ */
/* Wait up to ms (-1 = forever) for terminal input; 1 if bytes are buffered. */
static int ConFill(int ms) {
    fd_set fs;
    struct timeval tv;
    int r;
    ssize_t n;

    if (conInPos < conInLen) return 1;
    FD_ZERO(&fs);
    FD_SET(STDIN_FILENO, &fs);
    if (ms >= 0) { tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000; }
    r = select(STDIN_FILENO + 1, &fs, NULL, NULL, (ms >= 0) ? &tv : NULL);
    if (r <= 0) return 0;
    n = read(STDIN_FILENO, conInBuf, sizeof(conInBuf));
    if (n <= 0) {
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
        exit(1);                         /* terminal went away */
    }
    conInLen = (int)n;
    conInPos = 0;
    return 1;
}

static int ConReadByte(int ms) {
    if (!ConFill(ms)) return -1;
    return conInBuf[conInPos++];
}

/* function key number (1..12) -> DOS extended scan code */
static int ConFnScan(int n) {
    static const unsigned char altmenu[7] = { 0x1F, 0x20, 0x19, 0x14, 0x16, 0x18, 0x12 };
    if (n == 1)  return 0x3B;
    if (n >= 2 && n <= 8) return altmenu[n - 2];     /* F2..F8 = menu bar */
    if (n == 9)  return 0x43;
    if (n == 10) return 0x44;
    if (n == 11) return SCAN_F11;
    if (n == 12) return 0x86;
    return -1;
}

static int ConDecodeCSI(int *second) {
    int c, p[4], np = 0, acc = -1;
    p[0] = p[1] = p[2] = p[3] = 0;
    for (;;) {
        c = ConReadByte(40);
        if (c < 0) return -1;
        if (c >= '0' && c <= '9') { acc = (acc < 0 ? 0 : acc) * 10 + (c - '0'); continue; }
        if (c == ';') { if (np < 4) p[np++] = (acc < 0 ? 0 : acc); acc = -1; continue; }
        if (c >= 0x40 && c <= 0x7E) {
            if (np < 4) p[np++] = (acc < 0 ? 0 : acc);
            break;
        }
        if (c < 0x20 || c > 0x3F) return -1;      /* malformed */
    }
    switch (c) {
        case 'A': *second = 72; return 0;
        case 'B': *second = 80; return 0;
        case 'C': *second = 77; return 0;
        case 'D': *second = 75; return 0;
        case 'H': *second = 71; return 0;
        case 'F': *second = 79; return 0;
        case 'P': *second = ConFnScan(1); return 0;
        case 'Q': *second = ConFnScan(2); return 0;
        case 'R': *second = ConFnScan(3); return 0;
        case 'S': *second = ConFnScan(4); return 0;
        case '~':
            switch (p[0]) {
                case 1: case 7: *second = 71; return 0;
                case 2:  *second = 82; return 0;
                case 3:  *second = 83; return 0;
                case 4: case 8: *second = 79; return 0;
                case 5:  *second = 73; return 0;
                case 6:  *second = 81; return 0;
                case 11: case 12: case 13: case 14: case 15:
                    *second = ConFnScan(p[0] - 10); return 0;
                case 17: case 18: case 19: case 20: case 21:
                    *second = ConFnScan(p[0] - 11); return 0;
                case 23: *second = ConFnScan(11); return 0;
                case 24: *second = ConFnScan(12); return 0;
            }
            break;
    }
    return -1;
}

/* U+00DF etc: what a US Mac keyboard types for Option+letter when the
   terminal is NOT set to use Option as Meta. */
static int ConMacOptionLetter(unsigned cp) {
    switch (cp) {
        case 0x00DF: return 's';     /* Option+S */
        case 0x2202: return 'd';     /* Option+D */
        case 0x03C0: return 'p';     /* Option+P */
        case 0x2020: return 't';     /* Option+T */
        case 0x00F8: return 'o';     /* Option+O */
        case 0x02D9: return 'h';     /* Option+H */
    }
    return -1;
}

/* Returns first byte (0 = extended, scan code in *second) or -1 for input
   this program ignores. */
static int ConDecodeKey(int *second) {
    int c, c2;
    *second = -1;
    c = ConReadByte(0);
    if (c < 0) return -1;
    if (c == 0x1B) {
        c2 = ConReadByte(30);
        if (c2 < 0 || c2 == 0x1B) return 27;
        if (c2 == '[') return ConDecodeCSI(second);
        if (c2 == 'O') {
            c2 = ConReadByte(30);
            switch (c2) {
                case 'A': *second = 72; return 0;
                case 'B': *second = 80; return 0;
                case 'C': *second = 77; return 0;
                case 'D': *second = 75; return 0;
                case 'H': *second = 71; return 0;
                case 'F': *second = 79; return 0;
                case 'P': *second = ConFnScan(1); return 0;
                case 'Q': *second = ConFnScan(2); return 0;
                case 'R': *second = ConFnScan(3); return 0;
                case 'S': *second = ConFnScan(4); return 0;
            }
            return -1;
        }
        if (c2 >= 'a' && c2 <= 'z') { *second = ConAltScan[c2 - 'a']; return 0; }
        if (c2 >= 'A' && c2 <= 'Z') { *second = ConAltScan[c2 - 'A']; return 0; }
        return -1;
    }
    if (c == 0x7F) return 8;
    if (c == '\n') return 13;
    if (c >= 0xC0) {                         /* UTF-8 lead byte */
        unsigned cp;
        int more = (c >= 0xE0) ? 2 : 1, k, cc, letter;
        cp = (unsigned)(c & (more == 2 ? 0x0F : 0x1F));
        for (k = 0; k < more; k++) {
            cc = ConReadByte(20);
            if (cc < 0 || (cc & 0xC0) != 0x80) return -1;
            cp = (cp << 6) | (unsigned)(cc & 0x3F);
        }
        letter = ConMacOptionLetter(cp);
        if (letter >= 0) { *second = ConAltScan[letter - 'a']; return 0; }
        return -1;
    }
    if (c >= 0x80) return -1;
    return c;
}

/* housekeeping while idle: window size, deferred screen flush */
static void ConIdle(void) {
    unsigned long now = GetMsTicks();
    if (conWinch || (now - conLastPoll) >= 250) {
        conWinch = 0;
        conLastPoll = now;
        ConSyncSize();
    }
    if (conDirty && (now - conLastFlush) >= 25) ConFlush();
}

int kbhit(void) {
    if (conPendingKey >= 0 || conStash >= 0) return 1;
    ConIdle();
    for (;;) {
        if (!ConFill(0)) return 0;
        conStash = ConDecodeKey(&conStashSecond);
        if (conStash >= 0) return 1;
    }
}

int getch(void) {
    int first, second;
    if (conPendingKey >= 0) {
        first = conPendingKey;
        conPendingKey = -1;
        return first;
    }
    if (conStash >= 0) {
        first = conStash; second = conStashSecond;
        conStash = -1;
    } else {
        ConFlush();
        for (;;) {
            if (conWinch) {
                conWinch = 0;
                if (ConSyncSize()) { conPendingKey = SCAN_RESIZE; return 0; }
            }
            if (!ConFill(100)) {
                if (ConSyncSize()) { conPendingKey = SCAN_RESIZE; return 0; }
                continue;
            }
            first = ConDecodeKey(&second);
            if (first >= 0) break;
        }
    }
    if (first == 0 && second == SCAN_F11) { ConToggleMax(); second = SCAN_RESIZE; }
    if (first == 0) conPendingKey = second;
    return first;
}

/* ---- start-up / shut-down ----------------------------------------------- */
static void ConRestoreTerm(void) {
    static const char seq[] = "\033[0m\033[?7h\033[?25h\033[?1049l";
    if (!conActive) return;
    conActive = 0;
    if (write(STDOUT_FILENO, seq, sizeof(seq) - 1) < 0) { }
    if (conHaveTio) tcsetattr(STDIN_FILENO, TCSANOW, &conOldTio);
}

static void ConSigExit(int sig) {
    ConRestoreTerm();
    _exit(128 + sig);
}

static void ConSigWinch(int sig) { (void)sig; conWinch = 1; }

static void ConShutdown(void) {
    int y, x, last = -1, n;
    char u[4];
    CloseCOM1();
    ConRestoreTerm();
    /* If the last thing on screen was plain text (the exit banner, an
       error report) leave it in the scroll-back like a DOS program would. */
    if (conPlain) {
        for (y = 0; y < ConH; y++)
            for (x = 0; x < ConW; x++)
                if (conCells[y][x].ch != ' ') { last = y; break; }
        for (y = 0; y <= last; y++) {
            int e = ConW;
            while (e > 0 && conCells[y][e - 1].ch == ' ') e--;
            for (x = 0; x < e; x++) {
                n = ConUtf8(conCells[y][x].ch, u);
                fwrite(u, 1, (size_t)n, stdout);
            }
            fputc('\n', stdout);
        }
        fflush(stdout);
    }
}

void ConInit(void) {
    struct termios t;
    struct sigaction sa;
    int i;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "PROMPRO needs an interactive terminal.\n");
        exit(1);
    }
    if (tcgetattr(STDIN_FILENO, &conOldTio) == 0) conHaveTio = 1;
    t = conOldTio;
    t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INLCR | IGNCR | INPCK | ISTRIP | IXON | IXOFF);
    t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | IEXTEN);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSADRAIN, &t);

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = ConSigWinch;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = ConSigExit;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    ConOutStr("\033[?1049h\033[?7l\033[0m\033[2J");   /* alt screen, no wrap */
    ConOutWrite();
    conActive = 1;
    atexit(ConShutdown);

    {   /* terminal smaller than the 80x25 UI: ask it to grow */
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
            (ws.ws_col < CON_MINW || ws.ws_row < CON_MINH)) {
            ConSetWindow(ws.ws_col < CON_MINW ? CON_MINW : ws.ws_col,
                         ws.ws_row < CON_MINH ? CON_MINH : ws.ws_row);
            delay(150);
        }
    }
    ConSyncSize();
    conAttr = 7;
    clrscr();
    conForce = 1;
    _setcursortype(_NORMALCURSOR);
    ConFlush();
}
#define ConExitScreen() (textmode(C80), clrscr())
#endif /* PP_POSIX */

#if PP_DOS
/* ========================================================================
   MS-DOS CONSOLE LAYER (Turbo C / Turbo C++ conio)
   ------------------------------------------------------------------------
   The program is written against the Borland conio calls, so on DOS they
   are simply used as they are. Three small shims sit on top of them:

     getch()/kbhit()  - wrapped so F11 can switch between the 25-line and
                        43/50-line text modes and report the change to the
                        UI as the pseudo key SCAN_RESIZE, exactly like a
                        window resize on the other platforms.
     textmode(m)      - the program uses textmode(C80) only as "reset
                        attributes and clear the screen"; it must not
                        throw away a 43/50-line mode, so it is redirected
                        to ConResetMode().
     ConSetWindow()   - Options > Window Size: 25 lines, or 43/50 lines on
                        EGA/VGA (C4350).
   ======================================================================== */
#ifndef C4350
#define C4350 64
#endif

#define kbhit()        ConKbhit()
#define getch()        ConGetch()
#define textmode(m)    ConResetMode()
#define ConFlush()     ((void)0)

int  ConKbhit(void);
int  ConGetch(void);
void ConResetMode(void);
void ConExitScreen(void);

#if PP_IN(1)
static int dosPending = -1;
static int dosScreenSet = 0;          /* 43/50-line mode is active */

int ConKbhit(void) {
    if (dosPending >= 0) return 1;
    return (kbhit)();
}

void ConResetMode(void) {
    textattr(7);
    clrscr();
}

/* Read the text mode actually in effect back into ConW/ConH. */
static void ConReadSize(void) {
    struct text_info ti;
    gettextinfo(&ti);
    ConW = ti.screenwidth;
    ConH = ti.screenheight;
    if (ConW < CON_MINW) ConW = CON_MINW;
    if (ConH < CON_MINH) ConH = CON_MINH;
}

void ConSetWindow(int w, int h) {
    (void)w;
    if (h >= 43) {
        (textmode)(C4350);
        dosScreenSet = 1;
    } else {
        (textmode)(C80);
        dosScreenSet = 0;
    }
    ConReadSize();
    _setcursortype(_NORMALCURSOR);
    textattr(7);
    clrscr();
    conResized = 1;
}

int ConSyncSize(void) { return 0; }

void ConToggleMax(void) {
    if (!conMaxed) { ConSetWindow(80, 50); conMaxed = 1; }
    else           { ConSetWindow(80, 25); conMaxed = 0; }
}

int ConGetch(void) {
    int c;
    if (dosPending >= 0) {
        c = dosPending;
        dosPending = -1;
        return c;
    }
    c = (getch)();
    if (c == 0) {
        c = (getch)();
        if (c == SCAN_F11) { ConToggleMax(); c = SCAN_RESIZE; }
        dosPending = c;
        return 0;
    }
    return c;
}

/* Leave the screen in a sane state for COMMAND.COM. */
void ConExitScreen(void) {
    if (dosScreenSet) {
        (textmode)(C80);
        dosScreenSet = 0;
    }
    textattr(7);
    clrscr();
}

static void ConShutdown(void) {
    CloseCOM1();
    if (dosScreenSet) {
        (textmode)(C80);
        dosScreenSet = 0;
    }
    _setcursortype(_NORMALCURSOR);
}

void ConInit(void) {
    ConReadSize();
    textattr(7);
    clrscr();
    _setcursortype(_NORMALCURSOR);
    atexit(ConShutdown);
}
#endif /* PP_IN(1) */
#endif /* PP_DOS */



/* Buffer Configuration */
#define MAX_BUFFER_SIZE 65536UL
#define LIST_BPL 16              /* fixed width of saved/printed listings */
#define BYTES_PER_LINE BytesPerLine   /* on-screen width, follows window */

/* Edit Mode Definitions */
#define MODE_VIEW     0
#define MODE_EDIT_HEX 1
#define MODE_EDIT_ASCII 2

/* Cursor Area Definitions */
#define AREA_HEX   0
#define AREA_ASCII 1

/* User-defined programmer profiles */
#define MAX_PROFILES 8
#define CONFIG_FILENAME "PROMPRO.CFG"

/* Dialog box constants */
#define DLG_MAX_W  60
#define DLG_MAX_H  20
#define SCREEN_W   ConW
#define SCREEN_H   ConH

/* File browser dialog */
#define FB_MAX_FILES 200
#define FB_VISIBLE   10
#if PP_DOS
#define FB_NAMELEN   14          /* 8.3 name + NUL */
#else
#define FB_NAMELEN   MAX_PATH
#endif

/* ------------------------------------------------------------------------
   UI Color Palette - "System Status" style (PROMlink/UniSite look):
   gray chrome bars (menu bar, hint/status bars) over a black content
   window, single-line bordered dialogs with the title embedded in the
   top border. Cosmetic only - no effect on data handling.
   ------------------------------------------------------------------------ */
#define CLR_BANNER_BG   BLACK      /* row 1: program name / mode banner   */
#define CLR_BANNER_FG   WHITE
#define CLR_MENUBAR_BG  LIGHTGRAY  /* row 2 menu bar, help/status bars    */
#define CLR_MENUBAR_FG  BLACK
#define CLR_MENUBAR_HI  RED        /* mnemonic accent on the menu bar     */
#define CLR_WIN_BG      BLACK      /* hex grid / dialog interior          */
#define CLR_WIN_FG      LIGHTGRAY
#define CLR_WIN_HDR     WHITE      /* column header text                  */
#define CLR_WIN_RULE    LIGHTGRAY  /* box-drawing rule under the header   */
#define CLR_CURSOR_BG   WHITE      /* reverse-video cell cursor           */
#define CLR_CURSOR_FG   BLACK
#define CLR_DLG_BORDER  LIGHTGRAY
#define CLR_DLG_TITLE   WHITE
#define CLR_DLG_TEXT    WHITE
#define CLR_DLG_ITEM    YELLOW
#define CLR_DLG_SEL_BG  WHITE
#define CLR_DLG_SEL_FG  BLACK
#define CLR_DLG_ERR     LIGHTRED
#define CLR_DLG_OK      LIGHTGREEN

/* FED-style hex grid palette (cyan/white on black, teal status bar,
   brown footer) - used only within the hex grid area itself; the
   banner and pulldown menu bar above it keep their existing palette. */
#define CLR_FED_HDR      CYAN
#define CLR_FED_DATA     WHITE
#define CLR_FED_OFFSET   CYAN
#define CLR_FED_SBAR_BG  CYAN
#define CLR_FED_SBAR_FG  BLACK
#define CLR_FED_FOOTER   BROWN
#define CLR_FED_BOX      LIGHTRED   /* box border around hex/ascii grids */

/* ------------------------------------------------------------------------
   Pulldown Menu Bar - Setup/Data/Program/Tasks/Utilities/Options/Exit/Help,
   activated by Alt+<mnemonic letter> (real BIOS scan-code-set-1 Alt codes,
   distinct from the plain ASCII letters used by existing single-key
   hotkeys, so nothing here can collide with them).
   ------------------------------------------------------------------------ */
#define MENUBAR_COUNT 8
#define MB_SETUP      0
#define MB_DATA       1
#define MB_PROGRAM    2
#define MB_TASKS      3
#define MB_UTILITIES  4
#define MB_OPTIONS    5
#define MB_EXIT       6
#undef MB_HELP
#define MB_HELP       7

#define ALTSCAN_S 0x1F
#define ALTSCAN_D 0x20
#define ALTSCAN_P 0x19
#define ALTSCAN_T 0x14
#define ALTSCAN_U 0x16
#define ALTSCAN_O 0x18
#define ALTSCAN_E 0x12
#define ALTSCAN_H 0x23
#define SCAN_F1   0x3B

#define UPLOAD_BAR_WIDTH 50

/* Global Variables */
PP_VI(PP_BUFPTR DataBuffer, NULL);
PP_VI(unsigned long BufferSize, 0);
PP_VI(unsigned long CursorPos, 0);
PP_VI(unsigned long TopLine, 0);
PP_VI(int BytesPerLine, 16);   /* bytes per on-screen row (16/32/48/64) */
PP_VI(int EdLines, 16);        /* data rows on screen */
PP_VI(int EditMode, MODE_VIEW);
PP_VI(int Modified, 0);
PP_VI(int CursorArea, AREA_HEX);
PP_VI(unsigned char FillByte, 0xFF);
PP_VI(int GroupSize, 4); /* bytes per hex group: 1, 2, 4, 8, or 16 (16 = ungrouped) */

/* User-defined programmer profiles - persisted to CONFIG_FILENAME so
   they survive across sessions. ActiveProfile is an index into
   Profiles[], or -1 meaning "no profile selected, use the 9600-baud
   / manual-init-string default behavior". */
struct ProgProfile {
    char name[21];
    unsigned long baud;
    char initStr[41];
    char cmdRead[21];
    char cmdProgram[21];
    char cmdVerify[21];
    char cmdBlank[21];
};
PP_V(struct ProgProfile Profiles[MAX_PROFILES]);
PP_VI(int ProfileCount, 0);
PP_VI(int ActiveProfile, -1);

PP_VI(char CurrentFileName[80], "");
PP_VI(int ShowCommandHelp, 0);

/* File browser listing - filled by Plat_FbEnum() */
PP_V(char fbNames[FB_MAX_FILES][FB_NAMELEN]);
PP_V(unsigned long fbSizes[FB_MAX_FILES]);
PP_V(char fbIsDir[FB_MAX_FILES]);

/* Forward Declarations */
int  InitCOM1(unsigned long baud);
void ReadDataFromCOM1(void);
char NibbleToHex(unsigned char nibble);
void SaveBufferToFile(void);
void SaveBufferAsHex(void);
void SaveListing(void);
void PrintBuffer(void);
void EditByteHex(void);
void EditByteASCII(void);
void DisplayHexEditor(void);
void WriteCOM1(unsigned char byte);
void SendInitString(void);
void SendStr(const char *s);
void SendRepeat(unsigned char val, unsigned int count);
void SendHexByte(unsigned char b);
void SendHexWord(unsigned int w);
void SendHexLong(unsigned long v);
int  CheckAbort(unsigned long done, unsigned long total);
void DrawUploadDialog(const char *fmtDesc, unsigned long total);
void DrawReceiveDialog(unsigned long expected);
void UpdateReceiveBar(unsigned long received);
void UpdateUploadBar(unsigned long done, unsigned long total);
void UploadToEPROM(void);
void LoadFileIntoBuffer(void);
void SetWindowSize(void);

int  DlgDraw(int w, int h, const char *title);
void DlgLine(int screenRow, int dlgW, const char *text);
void DlgLineLeft(int screenRow, int dlgW, const char *text);
void DlgMenuItem(int screenRow, int dlgW, const char *text, int selected);
void DlgInputField(int screenRow, int dlgW, const char *label, const char *buf, int cursorVisible);
int  DlgInput(int screenRow, int dlgW, const char *label, char *buf, int maxLen);
int  DlgFileBrowser(const char *title, const char *filter, char *outFilename, int maxLen);
void DlgMessage(const char *title, const char *line1, const char *line2, int isError);
int  DlgYesNo(const char *title, const char *question);
int  DlgMenu(const char *title, const char **items, int count);
void ShowTitleScreen(void);
int ShowStartupMenu(void);
void HexEditor(void);
void GotoAddress(void);
void FindInBuffer(void);
void FillRange(void);
void SetFillByte(void);
void SetGroupSize(void);
void SaveProfiles(void);
void LoadProfiles(void);
void NewProgrammer(void);
void EditProgrammer(void);
void DeleteProgrammer(void);
void SelectProgrammer(void);
void SetComPort(void);
void SaveComPort(void);
void LoadComPort(void);
void InitConfigPaths(const char *argv0);
void DetectComPorts(void);
void DescribeComPorts(char *out, int outLen);
void ComPortLabel(char *out, int n);
int  ComPortPresent(void);
#if PP_POSIX
extern char DetectedDev[][80];
extern char ComDevPath[];
#else
extern int DetectedPorts[];
extern int ComPortExists(int n);
unsigned int ComBaseFor(int n);
#endif
extern int DetectedCount;
extern char ProfilePath[];
extern char ComCfgPath[];
int  Plat_ParseArg(int argc, char **argv, int *ai);
void Plat_AutoSelectPort(int given);
int  Plat_FbEnum(int wantDirs, const char *filter, int start);
void Plat_GetCwd(char *buf, int n);
void Plat_Chdir(const char *p);
int  Plat_AtRoot(const char *cwd);
FILE *Plat_PrintOpen(void);
void  Plat_PrintClose(FILE *fp);
void ComPurgeTx(void);
void ConInit(void);

int  DlgDrawAt(int startX, int startY, int w, int h, const char *title);
void PulldownItem(int screenRow, int startX, int dlgW, const char *text, int selected);
int  PulldownList(int col, int row, const char *title, const char **items, int count);
void ComputeMenuBarCols(void);
void DrawTopMenuBar(int activeCat);
void DisplayStartupScreen(void);
void DrawSmallBox(int x, int y, int w, int h, const char *label);
void DrawConnectionPanel(int topRow);
void DrawStatusField(int row, int boxStartX, const char *label, const char *value);
void DrawSystemStatusBox(void);
int  OpenCategoryStartup(int cat);
void OpenCategoryEditor(int cat);


/* ---- prototypes (generated) ---- */
void CloseCOM1(void);
void ComputeEditorLayout(void);
void FlushRxRingBuffer(void);
unsigned long GetMsTicks(void);
int RxByteAvailable(void);
unsigned char RxGetByte(void);

/* ------------------------------------------------------------------------
   Serial receive ring buffer (shared by all platforms)
   ------------------------------------------------------------------------ */
#define RXBUF_SIZE 4096

PP_V(unsigned char RxBuf[RXBUF_SIZE]);
PP_VI(unsigned int  RxHead, 0);   /* next slot PollCom() will write      */
PP_VI(unsigned int  RxTail, 0);   /* next slot the foreground will read  */
PP_VI(unsigned long RxOverrunCount, 0);
PP_VI(unsigned long RxUartErrorCount, 0);
#if PP_POSIX
char ComDevPath[256] = "";   /* serial device; Options > Serial Port or -p DEVICE */
#else
PP_VI(int ComPortNum, 1);    /* COM port number; Options > COM Port or /COMn */
#endif

void FlushRxRingBuffer(void);
int  RxByteAvailable(void);
unsigned char RxGetByte(void);

/* Menu bar / redraw-dispatch globals - set up after the prototypes above
   so the DisplayHexEditor default is a valid forward reference. */
PP_VI(void (*RedrawScreenFn)(void), DisplayHexEditor);
PP_VI(int RequestExit, 0);
PP_V(int MenuBarCol[MENUBAR_COUNT]);
static const char *MenuBarLabels[MENUBAR_COUNT] = {
    "Setup", "Data", "Program", "Tasks", "Utilities", "Options", "Exit", "Help"
};
static const int MenuBarAltScan[MENUBAR_COUNT] = {
    ALTSCAN_S, ALTSCAN_D, ALTSCAN_P, ALTSCAN_T, ALTSCAN_U, ALTSCAN_O, ALTSCAN_E, ALTSCAN_H
};
PP_VI(int UploadDlgRow, 0);
PP_VI(int UploadDlgW, 60);
PP_VI(unsigned long UploadTotal, 0);
PP_VI(int ReceiveDlgRow, 0);
PP_VI(int ReceiveDlgW, 60);
PP_VI(unsigned long ReceiveExpected, 0);

#if PP_WIN32
/* ------------------------------------------------------------------------
   Windows COM port receive
   ------------------------------------------------------------------------
   The DOS version needed an IRQ4 interrupt handler because a bare 8250
   UART has no receive FIFO - one byte of holding register and nothing
   else. On Windows the serial driver owns the UART and buffers received
   bytes in its own queue (SetupComm below asks for 16 KB), so the ISR,
   the 8259 PIC handling and the 8250-specific caveats are gone. The
   foreground loop simply polls the driver; bytes pass through a small
   local ring buffer so the ASCII-hex parser in ReadDataFromCOM1() is
   unchanged.

   RxOverrunCount     - driver input queue overflowed (CE_RXOVER)
   RxUartErrorCount   - hardware overrun / framing / parity / break
   ------------------------------------------------------------------------ */
HANDLE hCom = INVALID_HANDLE_VALUE;

/* ------------------------------------------------------------------------
   Serial port (Win32 comm API)
   ------------------------------------------------------------------------ */

/* Pull whatever the driver has queued into the local ring buffer. */
static void PollCom(void) {
    DWORD errs = 0, got = 0, i;
    COMSTAT st;
    unsigned int freeSpace, n;
    unsigned char tmp[512];

    if (hCom == INVALID_HANDLE_VALUE) return;

    if (ClearCommError(hCom, &errs, &st)) {
        if (errs & (CE_OVERRUN | CE_FRAME | CE_RXPARITY | CE_BREAK)) RxUartErrorCount++;
        if (errs & CE_RXOVER) RxOverrunCount++;
    }

    freeSpace = (RXBUF_SIZE - 1) - ((RxHead + RXBUF_SIZE - RxTail) % RXBUF_SIZE);
    n = (freeSpace > sizeof(tmp)) ? (unsigned int)sizeof(tmp) : freeSpace;
    if (n == 0) return;

    if (ReadFile(hCom, tmp, n, &got, NULL)) {
        for (i = 0; i < got; i++) {
            RxBuf[RxHead] = tmp[i];
            RxHead = (RxHead + 1) % RXBUF_SIZE;
        }
    }
}

void FlushRxRingBuffer(void) {
    if (hCom != INVALID_HANDLE_VALUE) PurgeComm(hCom, PURGE_RXCLEAR | PURGE_RXABORT);
    RxHead = 0;
    RxTail = 0;
    RxOverrunCount = 0;
    RxUartErrorCount = 0;
}

int RxByteAvailable(void) {
    if (RxHead == RxTail) PollCom();
    return (RxHead != RxTail);
}

unsigned char RxGetByte(void) {
    unsigned char b = 0;
    if (RxHead != RxTail) {
        b = RxBuf[RxTail];
        RxTail = (RxTail + 1) % RXBUF_SIZE;
    }
    return b;
}

/* Millisecond clock. Replaces the 18.2 Hz BIOS tick at 0040:006C - same
   idea (real elapsed time, independent of CPU speed), finer grain. */
unsigned long GetMsTicks(void) {
    return (unsigned long)GetTickCount();
}

void CloseCOM1(void) {
    if (hCom != INVALID_HANDLE_VALUE) {
        CloseHandle(hCom);
        hCom = INVALID_HANDLE_VALUE;
    }
}

/* Open \\.\COMn and configure 8N1 at the given baud, DTR+RTS asserted,
   no handshaking (same as the old MCR=0x0B / LCR=0x03 setup). Returns 1
   on success; on failure shows a dialog and returns 0. */
int InitCOM1(unsigned long baud) {
    char name[32];
    char msg[64];
    DCB dcb;
    COMMTIMEOUTS to;

    CloseCOM1();
    RxHead = 0;
    RxTail = 0;

    sprintf(name, "\\\\.\\COM%d", ComPortNum);
    hCom = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        char list[64];
        if (err == ERROR_FILE_NOT_FOUND)
            sprintf(msg, "COM%d does not exist on this PC.", ComPortNum);
        else if (err == ERROR_DEVICE_DOES_NOT_EXIST)
            sprintf(msg, "COM%d has no device behind it.", ComPortNum);
        else if (err == ERROR_ACCESS_DENIED)
            sprintf(msg, "COM%d is in use by another program.", ComPortNum);
        else
            sprintf(msg, "Cannot open COM%d (Windows error %lu).", ComPortNum, (unsigned long)err);
        DescribeComPorts(list, sizeof(list));
        DlgMessage(" Serial Port ", msg, list, 1);
        return 0;
    }

    SetupComm(hCom, 16384, 1024);

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hCom, &dcb)) {
        CloseCOM1();
        DlgMessage(" Serial Port ", "GetCommState failed - not a serial port?", "", 1);
        return 0;
    }
    dcb.BaudRate        = baud;
    dcb.ByteSize        = 8;
    dcb.Parity          = NOPARITY;
    dcb.StopBits        = ONESTOPBIT;
    dcb.fBinary         = 1;
    dcb.fParity         = 0;
    dcb.fOutxCtsFlow    = 0;
    dcb.fOutxDsrFlow    = 0;
    dcb.fDtrControl     = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = 0;
    dcb.fTXContinueOnXoff = 1;
    dcb.fOutX           = 0;
    dcb.fInX            = 0;
    dcb.fErrorChar      = 0;
    dcb.fNull           = 0;
    dcb.fRtsControl     = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError   = 0;
    if (!SetCommState(hCom, &dcb)) {
        CloseCOM1();
        DlgMessage(" Serial Port ", "SetCommState failed (bad baud rate?).", "", 1);
        return 0;
    }

    /* Reads return immediately with whatever is queued; writes give up
       after 1 s so a stalled link can't hang the program. */
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 1000;
    SetCommTimeouts(hCom, &to);

    PurgeComm(hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);

    /* Flush HERE, not in ReadDataFromCOM1(): every caller sends an init
       string / read command and may sit on a "press any key" prompt
       between here and the read, and a fast programmer's reply must
       survive that. This only discards pre-command leftovers/echo. */
    FlushRxRingBuffer();
    return 1;
}

void ComPurgeTx(void) {
    if (hCom != INVALID_HANDLE_VALUE) PurgeComm(hCom, PURGE_TXABORT | PURGE_TXCLEAR);
}

void WriteCOM1(unsigned char byte) {
    DWORD wr;
    if (hCom == INVALID_HANDLE_VALUE) return;
    WriteFile(hCom, &byte, 1, &wr, NULL);
}
#endif /* PP_WIN32 */

#if PP_POSIX
/* ------------------------------------------------------------------------
   Serial port (POSIX termios)
   ------------------------------------------------------------------------
   The kernel's tty driver buffers received bytes (and a USB serial adapter
   has its own FIFO), so the foreground simply polls the descriptor. Bytes
   pass through a small local ring buffer so the ASCII-hex parser in
   ReadDataFromCOM1() is unchanged.

   RxOverrunCount     - local ring buffer was full
   RxUartErrorCount   - not reported by termios in raw mode (always 0)
   ------------------------------------------------------------------------ */
static int comFd = -1;
static unsigned int comTxCount = 0;

static void PollCom(void) {
    unsigned char tmp[512];
    ssize_t n, i;
    unsigned int freeSpace, k;

    if (comFd < 0) return;
    freeSpace = (RXBUF_SIZE - 1) - ((RxHead + RXBUF_SIZE - RxTail) % RXBUF_SIZE);
    k = (freeSpace > sizeof(tmp)) ? (unsigned int)sizeof(tmp) : freeSpace;
    if (k == 0) return;
    n = read(comFd, tmp, k);
    if (n > 0) {
        for (i = 0; i < n; i++) {
            RxBuf[RxHead] = tmp[i];
            RxHead = (RxHead + 1) % RXBUF_SIZE;
        }
    }
}

void FlushRxRingBuffer(void) {
    if (comFd >= 0) tcflush(comFd, TCIFLUSH);
    RxHead = 0;
    RxTail = 0;
    RxOverrunCount = 0;
    RxUartErrorCount = 0;
}

int RxByteAvailable(void) {
    if (RxHead == RxTail) PollCom();
    return (RxHead != RxTail);
}

unsigned char RxGetByte(void) {
    unsigned char b = 0;
    if (RxHead != RxTail) {
        b = RxBuf[RxTail];
        RxTail = (RxTail + 1) % RXBUF_SIZE;
    }
    return b;
}

/* Millisecond clock (monotonic). */
unsigned long GetMsTicks(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000L);
}

void CloseCOM1(void) {
    if (comFd >= 0) {
        tcdrain(comFd);
        close(comFd);
        comFd = -1;
    }
}

void ComPurgeTx(void) {
    if (comFd >= 0) tcflush(comFd, TCOFLUSH);
}

/* Map a baud rate to a termios speed; 0 if this system lacks it. */
static speed_t BaudToSpeed(unsigned long baud) {
    switch (baud) {
        case 300:    return B300;
        case 600:    return B600;
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
    }
    return 0;
}

/* Open ComDevPath and configure 8N1 at the given baud, DTR+RTS asserted,
   no handshaking. Returns 1 on success; on failure shows a dialog and
   returns 0. */
int InitCOM1(unsigned long baud) {
    struct termios t;
    speed_t sp;
    int modem;
    char msg[96];

    CloseCOM1();
    RxHead = 0;
    RxTail = 0;

    if (ComDevPath[0] == '\0') {
        char list[96];
        DescribeComPorts(list, sizeof(list));
        DlgMessage(" Serial Port ", "No serial port selected.", list, 1);
        return 0;
    }
    sp = BaudToSpeed(baud);
    if (sp == 0) {
        sprintf(msg, "%lu baud is not supported here.", baud);
        DlgMessage(" Serial Port ", msg, "", 1);
        return 0;
    }
    comFd = open(ComDevPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (comFd < 0) {
        char tail[96];
        sprintf(msg, "Cannot open %.40s", ComDevPath);
        sprintf(tail, "%.90s", strerror(errno));
        DlgMessage(" Serial Port ", msg, tail, 1);
        return 0;
    }
#ifdef TIOCEXCL
    ioctl(comFd, TIOCEXCL);
#endif
    if (tcgetattr(comFd, &t) != 0) {
        CloseCOM1();
        DlgMessage(" Serial Port ", "Not a serial port (tcgetattr failed).", "", 1);
        return 0;
    }
    cfmakeraw(&t);
    t.c_cflag |= (tcflag_t)(CLOCAL | CREAD);
    t.c_cflag &= ~(tcflag_t)(CSTOPB | PARENB | CRTSCTS);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    cfsetispeed(&t, sp);
    cfsetospeed(&t, sp);
    if (tcsetattr(comFd, TCSANOW, &t) != 0) {
        CloseCOM1();
        DlgMessage(" Serial Port ", "tcsetattr failed (bad baud rate?).", "", 1);
        return 0;
    }
    modem = TIOCM_DTR | TIOCM_RTS;
    ioctl(comFd, TIOCMBIS, &modem);
    tcflush(comFd, TCIOFLUSH);
    comTxCount = 0;
    FlushRxRingBuffer();
    return 1;
}

void WriteCOM1(unsigned char byte) {
    int tries = 0;
    ssize_t r;
    if (comFd < 0) return;
    for (;;) {
        r = write(comFd, &byte, 1);
        if (r == 1) break;
        if (r < 0 && (errno == EAGAIN || errno == EINTR) && tries++ < 20) {
            fd_set ws;
            struct timeval tv;
            FD_ZERO(&ws);
            FD_SET(comFd, &ws);
            tv.tv_sec = 0; tv.tv_usec = 100000;
            select(comFd + 1, NULL, &ws, NULL, &tv);
            continue;
        }
        return;
    }
    if ((++comTxCount & 63u) == 0) tcdrain(comFd);   /* keep the progress bar honest */
}
#endif /* PP_POSIX */

#if PP_DOS
/* ------------------------------------------------------------------------
   Serial port (MS-DOS: 8250/16550 UART, interrupt driven)
   ------------------------------------------------------------------------
   A bare 8250 has a one-byte receive register, so polling from the
   foreground can drop characters whenever the screen code takes too long.
   An interrupt handler (IRQ4 for COM1/COM3, IRQ3 for COM2/COM4) moves
   every received byte into the ring buffer. A 16550 FIFO is switched on
   when the chip has one (14-byte trigger).

   RxOverrunCount     - ring buffer was full, a byte was dropped
   RxUartErrorCount   - UART overrun / parity / framing / break
   ------------------------------------------------------------------------ */
#if PP_IN(1)
static unsigned int  ComBase = 0;       /* UART base I/O address (0 = closed) */
static int           ComIrq = 4;
static int           ComIsrOn = 0;
static int           ComFifo = 0;
static void interrupt (*OldComIsr)(void);

static void interrupt ComIsr(void) {
    unsigned char iir, lsr, b;
    unsigned int next;
    int guard = 0;

    while (guard++ < 64) {
        iir = inportb(ComBase + 2);
        if (iir & 0x01) break;                   /* nothing pending */
        switch (iir & 0x06) {
            case 0x06:                           /* line status */
                lsr = inportb(ComBase + 5);
                if (lsr & 0x1E) RxUartErrorCount++;
                break;
            case 0x04:                           /* received data / FIFO timeout */
                while ((lsr = inportb(ComBase + 5)) & 0x01) {
                    if (lsr & 0x1E) RxUartErrorCount++;
                    b = inportb(ComBase);
                    next = (RxHead + 1) & (RXBUF_SIZE - 1);
                    if (next == RxTail) {
                        RxOverrunCount++;        /* ring full: drop */
                    } else {
                        RxBuf[RxHead] = b;
                        RxHead = next;
                    }
                }
                break;
            case 0x02:                           /* THR empty (not enabled) */
                break;
            default:                             /* modem status */
                (void)inportb(ComBase + 6);
                break;
        }
    }
    outportb(0x20, 0x20);                        /* EOI to the 8259 */
}

static void PollCom(void) { }                    /* the ISR does the work */

void CloseCOM1(void) {
    if (ComIsrOn) {
        disable();
        outportb(ComBase + 1, 0x00);             /* UART interrupts off */
        outportb(ComBase + 4, 0x00);             /* DTR/RTS/OUT2 off */
        outportb(0x21, inportb(0x21) | (unsigned char)(1 << ComIrq));
        setvect(ComIrq + 8, OldComIsr);
        enable();
        ComIsrOn = 0;
    }
    ComBase = 0;
}

void FlushRxRingBuffer(void) {
    disable();
    RxHead = 0;
    RxTail = 0;
    RxOverrunCount = 0;
    RxUartErrorCount = 0;
    enable();
}

int RxByteAvailable(void) {
    return (RxHead != RxTail);
}

unsigned char RxGetByte(void) {
    unsigned char b = 0;
    if (RxHead != RxTail) {
        b = RxBuf[RxTail];
        RxTail = (RxTail + 1) & (RXBUF_SIZE - 1);
    }
    return b;
}

/* Millisecond clock from the 18.2 Hz BIOS tick (55 ms per tick). Only
   differences are used, and a multiply wraps cleanly mod 2^32, so the
   subtraction in the callers stays correct. (The count restarts at
   midnight; a timeout that spans midnight is cut short, harmless.) */
unsigned long GetMsTicks(void) {
    return (unsigned long)biostime(0, 0L) * 55UL;
}

/* Wait for a byte to go out, then send it. */
void WriteCOM1(unsigned char byte) {
    unsigned long t0;
    if (ComBase == 0) return;
    t0 = GetMsTicks();
    while ((inportb(ComBase + 5) & 0x20) == 0) {
        if (GetMsTicks() - t0 > 1000UL) return;  /* stalled link */
    }
    outportb(ComBase, byte);
}

void ComPurgeTx(void) { }

/* COM1..COM4 base address from the BIOS data area (0040:0000), with the
   standard PC addresses as a fallback for COM1/COM2. */
unsigned int ComBaseFor(int n) {
    static const unsigned int std[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
    unsigned int a;
    if (n < 1 || n > 4) return 0;
    a = *(unsigned int far *)MK_FP(0x40, (n - 1) * 2);
    if (a == 0 && n <= 2) a = std[n - 1];
    return a;
}

int InitCOM1(unsigned long baud) {
    char msg[64];
    unsigned int div;

    CloseCOM1();
    RxHead = 0;
    RxTail = 0;

    ComBase = ComBaseFor(ComPortNum);
    if (ComBase == 0) {
        char list[64];
        sprintf(msg, "COM%d is not installed in this PC.", ComPortNum);
        DescribeComPorts(list, sizeof(list));
        DlgMessage(" Serial Port ", msg, list, 1);
        return 0;
    }
    if (baud < 50UL || baud > 115200UL) baud = 9600UL;
    div = (unsigned int)(115200UL / baud);
    ComIrq = (ComPortNum == 1 || ComPortNum == 3) ? 4 : 3;

    disable();
    OldComIsr = getvect(ComIrq + 8);
    setvect(ComIrq + 8, ComIsr);
    ComIsrOn = 1;

    outportb(ComBase + 1, 0x00);                 /* UART interrupts off */
    outportb(ComBase + 3, 0x80);                 /* DLAB */
    outportb(ComBase + 0, (unsigned char)(div & 0xFF));
    outportb(ComBase + 1, (unsigned char)(div >> 8));
    outportb(ComBase + 3, 0x03);                 /* 8N1 */
    outportb(ComBase + 2, 0xC7);                 /* FIFO on, 14-byte trigger */
    ComFifo = ((inportb(ComBase + 2) & 0xC0) == 0xC0);
    if (!ComFifo) outportb(ComBase + 2, 0x00);   /* 8250/16450: no FIFO */
    outportb(ComBase + 4, 0x0B);                 /* DTR + RTS + OUT2 (IRQ gate) */
    (void)inportb(ComBase + 5);                  /* clear any pending state */
    (void)inportb(ComBase);
    (void)inportb(ComBase + 6);
    (void)inportb(ComBase + 2);
    outportb(0x21, inportb(0x21) & (unsigned char)~(1 << ComIrq));
    outportb(ComBase + 1, 0x01);                 /* interrupt on received data */
    enable();

    FlushRxRingBuffer();
    return 1;
}
#endif /* PP_IN(1) */
#endif /* PP_DOS */

#if PP_DOS && PP_IN(1)
unsigned _stklen = 16384U;      /* conio + dialogs + sprintf buffers */
#endif

#if PP_IN(1)
void DrawReceiveDialog(unsigned long expected) {
    int r;
    char line[64];

    ReceiveExpected = expected;
    r = DlgDraw(ReceiveDlgW, 11, " Receiving from Programmer ");
    ReceiveDlgRow = r;

    if (expected > 0) sprintf(line, "Expecting: %lu bytes", expected);
    else strcpy(line, "Expecting: unknown (reads until idle)");
    DlgLineLeft(r,     ReceiveDlgW, line);
    DlgLineLeft(r + 1, ReceiveDlgW, "Format: ASCII space-separated hex");
    DlgLineLeft(r + 6, ReceiveDlgW, "Press ESC to abort");

    UpdateReceiveBar(0);
}

void UpdateReceiveBar(unsigned long received) {
    int i, filled, barX, pos;
    unsigned int pct;
    char stat[64];

    barX = (SCREEN_W - ReceiveDlgW) / 2 + 2;

    gotoxy(barX, ReceiveDlgRow + 3);
    textcolor(CLR_DLG_OK);
    textbackground(CLR_WIN_BG);

    if (ReceiveExpected > 0) {
        pct = (unsigned int)((received * 100UL) / ReceiveExpected);
        filled = (int)((received * (unsigned long)UPLOAD_BAR_WIDTH) / ReceiveExpected);
        if (filled > UPLOAD_BAR_WIDTH) filled = UPLOAD_BAR_WIDTH;
        for (i = 0; i < filled; i++) cprintf("\xDB");
        textcolor(CLR_DLG_BORDER);
        for (; i < UPLOAD_BAR_WIDTH; i++) cprintf("\xB0");
        sprintf(stat, "%lu / %lu bytes  (%u%%)", received, ReceiveExpected, pct);
    } else {
        pos = (int)(received % (unsigned long)UPLOAD_BAR_WIDTH);
        for (i = 0; i < UPLOAD_BAR_WIDTH; i++) cprintf((i == pos) ? "\xDB" : "\xB0");
        sprintf(stat, "%lu bytes received", received);
    }

    gotoxy(barX, ReceiveDlgRow + 4);
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
    cprintf("%-50s", stat);

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

void ReadDataFromCOM1(void) {
    char hexStr[3];
    int hexPos;
    int ch;
    unsigned long expectedSize;
    unsigned long lastBarUpdate;
    unsigned long lastByteTick;
    unsigned long nowTick;
    char sizeBuf[16];
    int w = 54, h = 9, r;

    sizeBuf[0] = '\0';
    r = DlgDraw(w, h, " Read from Programmer ");
    DlgLine(r,     w, "Enter expected size in bytes,");
    DlgLine(r + 1, w, "or leave blank to read until idle:");
    DlgInput(r + 3, w, "Size:", sizeBuf, 6);
    expectedSize = (sizeBuf[0] != '\0') ? (unsigned long)atol(sizeBuf) : 0;

    hexStr[0] = 0; hexStr[1] = 0; hexStr[2] = 0;
    hexPos        = 0;
    lastBarUpdate = 0;
    BufferSize    = 0;

    /* NOTE: the ring buffer is flushed in InitCOM1(), before the
       init string / read command goes out - not here. By the time
       we reach this function the command has already been sent and
       possibly answered (see InitCOM1 for why flushing at this point
       would throw away real data). */

    DrawReceiveDialog(expectedSize);
    lastByteTick = GetMsTicks();

    {
        int lineState = 0;
        int tokenLen = 0;
        int spaceCount = 0;

    while (BufferSize < MAX_BUFFER_SIZE) {

        /* Drain everything the ISR has queued up since we last looked.
           Because the ISR is what's actually touching the UART now,
           this can never miss a byte the way direct port polling did. */
        while (RxByteAvailable()) {
            ch = RxGetByte();
            lastByteTick = GetMsTicks();

            if (ch == '\r' || ch == '\n') {
                lineState = 0;
                tokenLen = 0;
                spaceCount = 0;
                hexPos = 0;
                continue;
            }

            if (lineState == 0) {
                int isHex = ((ch >= '0' && ch <= '9') ||
                             (ch >= 'A' && ch <= 'F') ||
                             (ch >= 'a' && ch <= 'f'));

                if (isHex && tokenLen < 7) {
                    tokenLen++;
                    if (hexPos < 2)
                        hexStr[hexPos++] = (char)ch;
                } else if (ch == ' ' || ch == '\t') {
                    spaceCount++;
                    if (tokenLen == 4 && spaceCount >= 2) {
                        lineState = 1;
                        hexPos = 0;
                    } else if (tokenLen == 2 && spaceCount >= 1) {
                        if (hexPos == 2) {
                            unsigned char hi = (unsigned char)hexStr[0];
                            unsigned char lo = (unsigned char)hexStr[1];
                            hi = (hi >= 'a') ? (hi-'a'+10) :
                                 (hi >= 'A') ? (hi-'A'+10) : (hi-'0');
                            lo = (lo >= 'a') ? (lo-'a'+10) :
                                 (lo >= 'A') ? (lo-'A'+10) : (lo-'0');
                            DataBuffer[BufferSize++] = (hi << 4) | lo;
                            if (expectedSize > 0 && BufferSize >= expectedSize)
                                break;
                        }
                        hexPos = 0;
                        lineState = 2;
                    } else if (tokenLen > 0 && tokenLen != 4) {
                        lineState = 2;
                    }
                } else if (!isHex) {
                    lineState = 2;
                    hexPos = 0;
                }
                continue;
            }

            if (lineState == 1) {
                lineState = 2;
            }

            if (lineState == 2) {
                if ((ch >= '0' && ch <= '9') ||
                    (ch >= 'A' && ch <= 'F') ||
                    (ch >= 'a' && ch <= 'f')) {
                    hexStr[hexPos++] = (char)ch;
                    if (hexPos == 2) {
                        unsigned char hi = (unsigned char)hexStr[0];
                        unsigned char lo = (unsigned char)hexStr[1];
                        hi = (hi >= 'a') ? (hi-'a'+10) :
                             (hi >= 'A') ? (hi-'A'+10) : (hi-'0');
                        lo = (lo >= 'a') ? (lo-'a'+10) :
                             (lo >= 'A') ? (lo-'A'+10) : (lo-'0');
                        DataBuffer[BufferSize++] = (hi << 4) | lo;
                        hexPos = 0;
                        if (expectedSize > 0 && BufferSize >= expectedSize)
                            break;
                    }
                } else if (ch == ' ' || ch == '\t') {
                    hexPos = 0;
                }
            }
        }

        if (expectedSize > 0 && BufferSize >= expectedSize) {
            UpdateReceiveBar(BufferSize);
            DlgLineLeft(ReceiveDlgRow + 6, ReceiveDlgW, "Complete - entering editor...  ");
            delay(600);
            break;
        }

        if (BufferSize >= lastBarUpdate + 8) {
            UpdateReceiveBar(BufferSize);
            lastBarUpdate = BufferSize;
        }

        if (kbhit()) {
            if (getch() == 27) {
                UpdateReceiveBar(BufferSize);
                DlgLineLeft(ReceiveDlgRow + 6, ReceiveDlgW, "Aborted by user.               ");
                delay(600);
                break;
            }
        }

        /* Idle-timeout, measured in real elapsed milliseconds
           rather than a busy-loop iteration count.
           An iteration count is implicitly calibrated to one specific
           CPU speed; on different hardware the same count is a
           completely different amount of wall time, which is what was
           cutting reads short on real 8250 hardware. */
        if (!RxByteAvailable()) {
            unsigned long idleTicks;
            unsigned long threshold;

            nowTick = GetMsTicks();
            idleTicks = nowTick - lastByteTick;  /* unsigned wrap-safe */

            if (expectedSize > 0)
                threshold = expectedSize / 2;
            else
                threshold = 64UL;

            /* ~2 seconds (2000 ms) of real silence after
               we've already got a meaningful amount of data means the
               programmer is actually done, not just paused between
               characters. Bump this up if your programmer has long
               inter-line pauses at low baud rates. */
            if (BufferSize >= threshold && idleTicks > 2000UL) {
                UpdateReceiveBar(BufferSize);
                DlgLineLeft(ReceiveDlgRow + 6, ReceiveDlgW, "Complete - entering editor...  ");
                delay(600);
                break;
            }

            delay(1);
        }

    }
    }

    UpdateReceiveBar(BufferSize);

    if (RxOverrunCount > 0 || RxUartErrorCount > 0) {
        char msg[64];
        sprintf(msg, "Warn: %lu ring-full, %lu UART err(OE/FE/PE/BI)",
                RxOverrunCount, RxUartErrorCount);
        DlgLineLeft(ReceiveDlgRow + 5, ReceiveDlgW, msg);
        delay(1500);
    }

    if (BufferSize >= MAX_BUFFER_SIZE) {
        DlgLineLeft(ReceiveDlgRow + 6, ReceiveDlgW, "Buffer full - reading stopped. ");
        delay(600);
    }
}

char NibbleToHex(unsigned char nibble) {
    return (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
}

void SaveBufferToFile(void) {
    char filename[MAX_PATH];
    FILE *fp;

    filename[0] = '\0';
    if (!DlgFileBrowser(" Save Binary File ", "*.*", filename, 48)) {
        DisplayHexEditor(); return;
    }
    if (filename[0] == '\0') { DisplayHexEditor(); return; }

    fp = fopen(filename, "wb");
    if (fp == NULL) {
        DlgMessage(" Error ", "Cannot create file!", filename, 1);
        return;
    }
    strncpy(CurrentFileName, filename, 79);
    CurrentFileName[79] = '\0';

    {
        unsigned char nearBuf[512];
        unsigned long written = 0;
        unsigned int chunk, j;
        while (written < BufferSize) {
            chunk = (BufferSize - written > 512UL) ? 512 : (unsigned int)(BufferSize - written);
            for (j = 0; j < chunk; j++)
                nearBuf[j] = DataBuffer[written + j];
            fwrite(nearBuf, 1, chunk, fp);
            written += chunk;
        }
    }

    fclose(fp);
    {
        char msg[50];
        sprintf(msg, "Saved %lu bytes OK", BufferSize);
        DlgMessage(" Saved ", msg, filename, 0);
    }
    Modified = 0;
}

void SaveBufferAsHex(void) {
    char filename[MAX_PATH];
    FILE *fp;

    {
        filename[0] = '\0';
        if (!DlgFileBrowser(" Save Intel HEX File ", "*.HEX", filename, 48)) {
            DisplayHexEditor(); return;
        }
    }
    if (filename[0] == '\0') { DisplayHexEditor(); return; }

    fp = fopen(filename, "w");
    if (fp == NULL) {
        DlgMessage(" Error ", "Cannot create file!", filename, 1);
        return;
    }

    {
        unsigned long laddr = 0;
        unsigned int bline, cs, j;
        unsigned char b;
        while (laddr < BufferSize) {
            bline = (BufferSize - laddr >= 16) ? 16 : (unsigned int)(BufferSize - laddr);
            cs = bline + ((laddr >> 8) & 0xFF) + (laddr & 0xFF);
            fprintf(fp, ":%02X%04lX00", bline, laddr);
            for (j = 0; j < bline; j++) {
                b = DataBuffer[laddr + j];
                fprintf(fp, "%02X", b);
                cs += b;
            }
            cs = (~cs + 1) & 0xFF;
            fprintf(fp, "%02X\n", cs);
            laddr += bline;
        }
    }
    fprintf(fp, ":00000001FF\n");
    fclose(fp);

    {
        char msg[50];
        sprintf(msg, "Intel HEX saved: %lu bytes", BufferSize);
        DlgMessage(" Saved ", msg, filename, 0);
    }
}

void SaveListing(void) {
    char filename[MAX_PATH];
    FILE *fp;
    unsigned long addr;
    unsigned int col;
    unsigned char byte;
    int yPos = 24;

    {
        filename[0] = '\0';
        if (!DlgFileBrowser(" Save Hex Listing ", "*.*", filename, 48)) {
            DisplayHexEditor(); return;
        }
    }
    if (filename[0] == '\0') { DisplayHexEditor(); return; }

    fp = fopen(filename, "w");
    if (fp == NULL) {
        gotoxy(1, yPos); clreol();
        textcolor(LIGHTRED);
        cprintf("ERROR: Cannot create file! Press any key...");
        textcolor(LIGHTGRAY);
        getch(); return;
    }

    fprintf(fp, "EPROM Data Listing\n");
    fprintf(fp, "==================\n\n");
    fprintf(fp, "Total bytes: %lu\n\n", BufferSize);
    fprintf(fp, "Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  ASCII\n");
    fprintf(fp, "------------------------------------------------------------------------\n");

    for (addr = 0; addr < BufferSize; addr += LIST_BPL) {
        fprintf(fp, "%04lX:    ", addr);
        for (col = 0; col < LIST_BPL && addr + col < BufferSize; col++) {
            byte = DataBuffer[addr + col];
            fprintf(fp, "%02X ", byte);
        }
        for (; col < LIST_BPL; col++) fprintf(fp, "   ");
        fprintf(fp, " ");
        for (col = 0; col < LIST_BPL && addr + col < BufferSize; col++) {
            byte = DataBuffer[addr + col];
            fprintf(fp, "%c", (byte >= 32 && byte < 127) ? byte : '.');
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
    gotoxy(1, yPos); clreol();
    textcolor(LIGHTGREEN);
    cprintf("Listing saved! (%lu bytes) Press any key...", BufferSize);
    textcolor(LIGHTGRAY);
    getch();
}

void PrintBuffer(void) {
    FILE *fp;
    unsigned long addr;
    unsigned int col;
    unsigned char byte;

    if (!DlgYesNo(" Print ", "Print buffer to " PP_PRINTER "?"))
        return;

    {
        int w = 44, h = 6;
        DlgDraw(w, h, " Printing ");
        DlgLine((SCREEN_H - h) / 2 + 3, w, "Sending to " PP_PRINTER "... ESC=cancel");
    }

    fp = Plat_PrintOpen();
    if (fp == NULL) {
        DlgMessage(" Error ", "Cannot access " PP_PRINTER "!", "Check printer.", 1);
        return;
    }

    fprintf(fp, "EPROM Data Listing\n");
    fprintf(fp, "==================\n\n");
    fprintf(fp, "Total bytes: %lu\n\n", BufferSize);
    fprintf(fp, "Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  ASCII\n");
    fprintf(fp, "------------------------------------------------------------------------\n");

    for (addr = 0; addr < BufferSize; addr += LIST_BPL) {
        if (kbhit() && getch() == 27) {
            Plat_PrintClose(fp);
            DlgMessage(" Cancelled ", "Printing cancelled.", "", 0);
            return;
        }
        fprintf(fp, "%04lX:    ", addr);
        for (col = 0; col < LIST_BPL && addr + col < BufferSize; col++) {
            byte = DataBuffer[addr + col];
            fprintf(fp, "%02X ", byte);
        }
        for (; col < LIST_BPL; col++) fprintf(fp, "   ");
        fprintf(fp, " ");
        for (col = 0; col < LIST_BPL && addr + col < BufferSize; col++) {
            byte = DataBuffer[addr + col];
            fprintf(fp, "%c", (byte >= 32 && byte < 127) ? byte : '.');
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "\f");
    Plat_PrintClose(fp);
    DlgMessage(" Done ", "Printing complete!", "", 0);
}

void EditByteHex(void) {
    char hexStr[3];
    int hexPos = 0;
    unsigned int value;
    int ch;
    int yPos = 24;

    hexStr[0] = 0; hexStr[1] = 0; hexStr[2] = 0;

    gotoxy(1, yPos);
    textcolor(YELLOW);
    textbackground(CLR_WIN_BG);
    clreol();
    cprintf("HEX EDIT - Enter 2 hex digits (or ESC): ");

    while (hexPos < 2) {
        ch = getch();
        if (ch == 0 || ch == 224) { getch(); continue; }
        if (ch == 27) return;
        else if ((ch >= '0' && ch <= '9') ||
                 (ch >= 'A' && ch <= 'F') ||
                 (ch >= 'a' && ch <= 'f')) {
            hexStr[hexPos++] = ch;
            cprintf("%c", ch);
        }
    }

    hexStr[2] = '\0';
    sscanf(hexStr, "%x", &value);
    DataBuffer[CursorPos] = (unsigned char)value;
    Modified = 1;
    if (CursorPos < BufferSize - 1) CursorPos++;
}

void EditByteASCII(void) {
    int ch;
    int yPos = 24;

    gotoxy(1, yPos);
    textcolor(YELLOW);
    textbackground(CLR_WIN_BG);
    clreol();
    cprintf("ASCII EDIT - Enter character (or ESC): ");

    ch = getch();
    if (ch == 0 || ch == 224) { getch(); return; }
    if (ch == 27) return;
    else if (ch >= 0 && ch < 256) {
        DataBuffer[CursorPos] = (unsigned char)ch;
        Modified = 1;
        cprintf("%c", ch);
        delay(200);
        if (CursorPos < BufferSize - 1) CursorPos++;
    }
}

/* ========================================================================
   DIALOG BOX ENGINE
   ======================================================================== */

int DlgDrawAt(int startX, int startY, int w, int h, const char *title) {
    int y, i;
    int tlen, remain, padL, padR;

    if (w > DLG_MAX_W) w = DLG_MAX_W;
    if (h > DLG_MAX_H) h = DLG_MAX_H;

    textcolor(CLR_DLG_BORDER);
    textbackground(CLR_WIN_BG);

    /* Top border: single-line box with the title embedded between tee
       connectors, e.g. DA-C4..-C3 Title B4-C4..-BF ("System Status" look) */
    tlen = strlen(title);
    remain = w - 4 - tlen;
    if (remain < 0) remain = 0;
    padL = remain / 2;
    padR = remain - padL;

    gotoxy(startX, startY);
    cprintf("\xDA");
    for (i = 0; i < padL; i++) cprintf("\xC4");
    cprintf("\xC3");
    textcolor(CLR_DLG_TITLE);
    cprintf("%s", title);
    textcolor(CLR_DLG_BORDER);
    cprintf("\xB4");
    for (i = 0; i < padR; i++) cprintf("\xC4");
    cprintf("\xBF");

    for (y = startY + 1; y < startY + h - 1; y++) {
        gotoxy(startX, y);
        cprintf("\xB3");
        for (i = 0; i < w - 2; i++) cprintf(" ");
        cprintf("\xB3");
    }

    gotoxy(startX, startY + h - 1);
    cprintf("\xC0");
    for (i = 0; i < w - 2; i++) cprintf("\xC4");
    cprintf("\xD9");

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);

    return startY + 1;
}

int DlgDraw(int w, int h, const char *title) {
    int startX, startY;
    if (w > DLG_MAX_W) w = DLG_MAX_W;
    if (h > DLG_MAX_H) h = DLG_MAX_H;
    startX = (SCREEN_W - w) / 2 + 1;
    startY = (SCREEN_H - h) / 2;
    return DlgDrawAt(startX, startY, w, h, title);
}

void DlgLine(int screenRow, int dlgW, const char *text) {
    int startX = (SCREEN_W - dlgW) / 2 + 1;
    int tlen = strlen(text);
    int col;
    if (tlen > dlgW - 2) tlen = dlgW - 2;
    col = startX + 1 + (dlgW - 2 - tlen) / 2;
    textcolor(CLR_DLG_TEXT);
    textbackground(CLR_WIN_BG);
    gotoxy(col, screenRow);
    cprintf("%.*s", tlen, text);
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

void DlgLineLeft(int screenRow, int dlgW, const char *text) {
    int startX = (SCREEN_W - dlgW) / 2 + 2;
    textcolor(CLR_DLG_TEXT);
    textbackground(CLR_WIN_BG);
    gotoxy(startX, screenRow);
    cprintf("%.*s", dlgW - 2, text);
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

void DlgMenuItem(int screenRow, int dlgW, const char *text, int selected) {
    int startX = (SCREEN_W - dlgW) / 2 + 2;
    int inner = dlgW - 4;
    int i;
    if (selected) { textcolor(CLR_DLG_SEL_FG); textbackground(CLR_DLG_SEL_BG); }
    else          { textcolor(CLR_DLG_ITEM);   textbackground(CLR_WIN_BG); }
    gotoxy(startX, screenRow);
    cprintf(" %s", text);
    for (i = strlen(text) + 1; i < inner; i++) cprintf(" ");
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

void DlgInputField(int screenRow, int dlgW, const char *label,
                           const char *buf, int cursorVisible) {
    int startX = (SCREEN_W - dlgW) / 2 + 2;
    int inner  = dlgW - 4;
    int lablen = strlen(label);
    int fieldW = inner - lablen - 1;
    int buflen = strlen(buf);
    int i;

    textcolor(CLR_DLG_TEXT); textbackground(CLR_WIN_BG);
    gotoxy(startX, screenRow);
    cprintf("%s", label);
    textcolor(CLR_WIN_HDR); textbackground(CLR_WIN_BG);
    cprintf(" ");
    cprintf("%s", buf);
    for (i = buflen; i < fieldW; i++) cprintf(" ");
    if (cursorVisible && buflen < fieldW) {
        gotoxy(startX + lablen + 1 + buflen, screenRow);
        textcolor(CLR_CURSOR_FG); textbackground(CLR_CURSOR_BG);
        cprintf(" ");
    }
    textcolor(CLR_WIN_FG); textbackground(CLR_WIN_BG);
}

int DlgInput(int screenRow, int dlgW, const char *label,
                    char *buf, int maxLen) {
    int len = strlen(buf);
    int ch;
    DlgInputField(screenRow, dlgW, label, buf, 1);
    while (1) {
        ch = getch();
        if (ch == 0 || ch == 224) { getch(); continue; }
        if (ch == 27) { buf[0] = '\0'; return 0; }
        if (ch == 13) return 1;
        if (ch == 8 && len > 0) {
            buf[--len] = '\0';
            DlgInputField(screenRow, dlgW, label, buf, 1);
        } else if (ch >= 32 && ch < 127 && len < maxLen) {
            buf[len++] = (char)ch;
            buf[len]   = '\0';
            DlgInputField(screenRow, dlgW, label, buf, 1);
        }
    }
}

/* Graphical file picker: lists files AND subdirectories in the current
   directory matching filter (e.g. "*.*", "*.HEX"), directories first
   then files, each group alphabetical (case-insensitive). A ".." entry
   is always first (except at a drive root) so Enter on it, or on any
   other directory, navigates there and rescans. Long file names are
   supported. Scrollable with Up/Down/PgUp/PgDn/Home/End. ENTER on a file
   selects it; N drops into a typed-filename entry (for a new file, or
   one that doesn't match the filter); ESC cancels and restores the
   directory you started in.
   Returns 1 with outFilename filled in (bare name - the process's
   current directory is left wherever that file lives, so the plain
   fopen(filename) calls elsewhere in the program still resolve
   correctly), or 0 on cancel. outFilename must hold MAX_PATH chars;
   maxLen limits only typed entry (the dialog field is 50 wide). Static
   arrays, not stack, hold the directory listing. */
int DlgFileBrowser(const char *title, const char *filter, char *outFilename, int maxLen) {
    int fbCount, dirCount, fileCount;
    char startDir[MAX_PATH];
    char curDir[MAX_PATH];
    int i, j;
    int sel, top;
    int w = 60, h = FB_VISIBLE + 6;
    int r, ch;
    char line[80];
    int atRoot;
    int inLen;

    inLen = (maxLen > 48) ? 48 : maxLen;
    Plat_GetCwd(startDir, (int)sizeof(startDir));

    while (1) {
        Plat_GetCwd(curDir, (int)sizeof(curDir));
        /* the root (C:\ or /) is as far up as we let '..' take you. */
        atRoot = Plat_AtRoot(curDir);

        /* --- scan this directory: subdirectories first, then files ---
           Capped one short of FB_MAX_FILES so a slot is always reserved
           for '..' below. Hidden/system entries are skipped. */
        dirCount = Plat_FbEnum(1, "*", 0);
        for (i = 1; i < dirCount; i++) {
            char tmpName[FB_NAMELEN];
            strcpy(tmpName, fbNames[i]);
            j = i - 1;
            while (j >= 0 && PP_STRICMP(fbNames[j], tmpName) > 0) {
                strcpy(fbNames[j + 1], fbNames[j]);
                j--;
            }
            strcpy(fbNames[j + 1], tmpName);
        }

        fileCount = Plat_FbEnum(0, filter, dirCount);
        for (i = 1; i < fileCount; i++) {
            char tmpName[FB_NAMELEN];
            unsigned long tmpSize;
            int base = dirCount;
            strcpy(tmpName, fbNames[base + i]);
            tmpSize = fbSizes[base + i];
            j = i - 1;
            while (j >= 0 && PP_STRICMP(fbNames[base + j], tmpName) > 0) {
                strcpy(fbNames[base + j + 1], fbNames[base + j]);
                fbSizes[base + j + 1] = fbSizes[base + j];
                j--;
            }
            strcpy(fbNames[base + j + 1], tmpName);
            fbSizes[base + j + 1] = tmpSize;
        }

        /* Inject ".." as entry 0 unless we're at a drive root. */
        if (!atRoot && dirCount + fileCount < FB_MAX_FILES) {
            for (i = dirCount + fileCount; i > 0; i--) {
                strcpy(fbNames[i], fbNames[i - 1]);
                fbSizes[i] = fbSizes[i - 1];
                fbIsDir[i] = fbIsDir[i - 1];
            }
            strcpy(fbNames[0], "..");
            fbIsDir[0] = 1;
            dirCount++;
        }
        fbCount = dirCount + fileCount;

        outFilename[0] = '\0';

        if (fbCount == 0) {
            /* Nothing here at all - go straight to manual entry. */
            r = DlgDraw(w, 9, title);
            DlgLine(r,     w, "No matching files in this directory.");
            DlgLine(r + 1, w, "Type a filename, or ESC to cancel:");
            if (DlgInput(r + 3, w, "File:", outFilename, inLen)) return 1;
            Plat_Chdir(startDir);
            return 0;
        }

        sel = 0; top = 0;
        r = DlgDraw(w, h, title);
        while (1) {
            char pathLine[70];
            size_t dl = strlen(curDir);
            if (dl > 55) sprintf(pathLine, "Dir: ...%s", curDir + dl - 52);
            else         sprintf(pathLine, "Dir: %s", curDir);
            DlgLineLeft(r, w, pathLine);

            if (sel < 0) sel = 0;
            if (sel > fbCount - 1) sel = fbCount - 1;
            if (sel < top) top = sel;
            if (sel >= top + FB_VISIBLE) top = sel - FB_VISIBLE + 1;

            for (i = 0; i < FB_VISIBLE; i++) {
                int idx = top + i;
                if (idx < fbCount) {
                    if (fbIsDir[idx])
                        sprintf(line, "%-34.34s <DIR>", fbNames[idx]);
                    else
                        sprintf(line, "%-34.34s %9lu bytes", fbNames[idx], fbSizes[idx]);
                    DlgMenuItem(r + 1 + i, w, line, idx == sel);
                } else {
                    DlgMenuItem(r + 1 + i, w, "", 0);
                }
            }
            {
                char status[70];
                sprintf(status, "%d of %d - ENTER=Select  N=Type Name  ESC=Cancel",
                        sel + 1, fbCount);
                DlgLineLeft(r + FB_VISIBLE + 2, w, status);
            }

            ch = getch();
            if (ch == 0 || ch == 224) {
                ch = getch();
                if (ch == 72) sel--;                                   /* up */
                else if (ch == 80) sel++;                              /* down */
                else if (ch == 73) sel -= FB_VISIBLE;                  /* PgUp */
                else if (ch == 81) sel += FB_VISIBLE;                  /* PgDn */
                else if (ch == 71) sel = 0;                            /* Home */
                else if (ch == 79) sel = fbCount - 1;                  /* End */
            } else if (ch == 13) {
                if (fbIsDir[sel]) {
                    Plat_Chdir(fbNames[sel]);
                    break;  /* rescan the new directory */
                }
                strncpy(outFilename, fbNames[sel], MAX_PATH - 1);
                outFilename[MAX_PATH - 1] = '\0';
                RedrawScreenFn();
                return 1;
            } else if (ch == 27) {
                Plat_Chdir(startDir);
                RedrawScreenFn();
                return 0;
            } else if (ch == 'n' || ch == 'N') {
                int rr2 = DlgDraw(w, 9, title);
                outFilename[0] = '\0';
                DlgLine(rr2,     w, "Type a filename:");
                DlgLine(rr2 + 1, w, "(ESC to cancel)");
                if (DlgInput(rr2 + 3, w, "File:", outFilename, inLen)) {
                    RedrawScreenFn();
                    return outFilename[0] != '\0';
                }
                r = DlgDraw(w, h, title);  /* cancelled manual entry - back to the list */
            }
        }
        /* fell through from an Enter-on-directory: loop rescans curDir */
    }
}

#endif /* PP_IN(1) */

#if PP_IN(2)
void DlgMessage(const char *title, const char *line1,
                        const char *line2, int isError) {
    int w = 50, h = 8;
    int r = DlgDraw(w, h, title);
    if (isError) { textcolor(CLR_DLG_ERR); textbackground(CLR_WIN_BG); }
    DlgLine(r,     w, line1);
    if (line2 && line2[0]) DlgLine(r + 1, w, line2);
    DlgLine(r + 3, w, "[ Press any key ]");
    textcolor(CLR_WIN_FG); textbackground(CLR_WIN_BG);
    getch();
    RedrawScreenFn();
}

int DlgYesNo(const char *title, const char *question) {
    int w = 50, h = 8;
    int r = DlgDraw(w, h, title);
    int sel = 0, ch;
    DlgLine(r, w, question);
    while (1) {
        DlgMenuItem(r + 2, w, "  Yes  ", sel == 0);
        DlgMenuItem(r + 3, w, "  No   ", sel == 1);
        ch = getch();
        if (ch == 0 || ch == 224) ch = getch();
        if (ch == 72 || ch == 75) sel = 0;
        if (ch == 80 || ch == 77) sel = 1;
        if (ch == 'y' || ch == 'Y') { RedrawScreenFn(); return 1; }
        if (ch == 'n' || ch == 'N') { RedrawScreenFn(); return 0; }
        if (ch == 27) { RedrawScreenFn(); return 0; }
        if (ch == 13) { RedrawScreenFn(); return sel == 0; }
    }
}

int DlgMenu(const char *title, const char **items, int count) {
    int w = 54;
    int h = count + 6;
    int r = DlgDraw(w, h, title);
    int sel = 0, ch, i;
    while (1) {
        for (i = 0; i < count; i++)
            DlgMenuItem(r + i, w, items[i], i == sel);
        ch = getch();
        if (ch == 0 || ch == 224) ch = getch();
        if ((ch == 72 || ch == 75) && sel > 0) sel--;
        if ((ch == 80 || ch == 77) && sel < count - 1) sel++;
        if (ch >= '1' && ch <= '0' + count) {
            RedrawScreenFn(); return ch - '0';
        }
        if (ch == 27) { RedrawScreenFn(); return 0; }
        if (ch == 13) { RedrawScreenFn(); return sel + 1; }
    }
}

/* ========================================================================
   PULLDOWN MENU BAR
   ======================================================================== */

void PulldownItem(int screenRow, int startX, int dlgW, const char *text, int selected) {
    int inner = dlgW - 2;
    int i;
    if (selected) { textcolor(CLR_DLG_SEL_FG); textbackground(CLR_DLG_SEL_BG); }
    else          { textcolor(CLR_DLG_ITEM);   textbackground(CLR_WIN_BG); }
    gotoxy(startX + 1, screenRow);
    cprintf(" %s", text);
    for (i = strlen(text) + 1; i < inner; i++) cprintf(" ");
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

int PulldownList(int col, int row, const char *title, const char **items, int count) {
    int w, h, r, sel = 0, ch, i, maxlen = 0, tlen, startX;

    for (i = 0; i < count; i++) {
        int l = strlen(items[i]);
        if (l > maxlen) maxlen = l;
    }
    w = maxlen + 4;
    tlen = strlen(title);
    if (w < tlen + 4) w = tlen + 4;
    if (w > DLG_MAX_W) w = DLG_MAX_W;
    h = count + 2;

    startX = col;
    if (startX + w > SCREEN_W + 1) startX = SCREEN_W - w + 1;
    if (startX < 1) startX = 1;

    r = DlgDrawAt(startX, row, w, h, title);
    while (1) {
        for (i = 0; i < count; i++)
            PulldownItem(r + i, startX, w, items[i], i == sel);
        ch = getch();
        if (ch == 0 || ch == 224) {
            ch = getch();
            if (ch == 72 && sel > 0) sel--;
            if (ch == 80 && sel < count - 1) sel++;
            for (i = 0; i < MENUBAR_COUNT; i++) {
                if (ch == MenuBarAltScan[i]) return -(i + 1);
            }
        } else if (ch == 13) {
            return sel + 1;
        } else if (ch == 27) {
            return 0;
        }
    }
}

void ComputeMenuBarCols(void) {
    int i, col = 1;
    for (i = 0; i < MENUBAR_COUNT; i++) {
        if (i == MB_HELP) {
            MenuBarCol[i] = SCREEN_W - (int)strlen(MenuBarLabels[i]) - 2;
        } else {
            MenuBarCol[i] = col;
            col += strlen(MenuBarLabels[i]) + 3;
        }
    }
}

void DrawTopMenuBar(int activeCat) {
    int i;
    gotoxy(1, 2);
    textcolor(CLR_MENUBAR_FG);
    textbackground(CLR_MENUBAR_BG);
    cprintf("%-*s", ConW, "");
    for (i = 0; i < MENUBAR_COUNT; i++) {
        gotoxy(MenuBarCol[i], 2);
        if (i == activeCat) { textcolor(CLR_DLG_SEL_FG); textbackground(CLR_DLG_SEL_BG); }
        else                { textcolor(CLR_MENUBAR_FG); textbackground(CLR_MENUBAR_BG); }
        cprintf(" %s ", MenuBarLabels[i]);
        if (i != activeCat) {
            gotoxy(MenuBarCol[i] + 1, 2);
            textcolor(CLR_MENUBAR_HI);
            cprintf("%c", MenuBarLabels[i][0]);
        }
    }
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

static int LastBPL = 16;

/* ComputeEditorLayout - derive the hex grid geometry from the window size.
   Rows: everything above/below the grid takes 9 rows, the rest is data.
   Columns: the widest of 64/48/32/16 bytes per line that fits is used.
   The scroll position is re-based so the cursor stays on screen. */
void ComputeEditorLayout(void) {
    static const int cand[4] = { 64, 48, 32, 16 };
    int i, bpl = 16, gs, hw;
    unsigned long curLine;

    for (i = 0; i < 4; i++) {
        gs = (GroupSize >= 16) ? cand[i] : GroupSize;
        hw = cand[i] * 2 + (cand[i] / gs - 1);
        if (14 + hw + cand[i] <= ConW) { bpl = cand[i]; break; }
    }
    BytesPerLine = bpl;
    EdLines = ConH - 9;
    if (EdLines < 1) EdLines = 1;
    if (LastBPL != BytesPerLine) {
        TopLine = (TopLine * (unsigned long)LastBPL) / (unsigned long)BytesPerLine;
        LastBPL = BytesPerLine;
    }
    curLine = CursorPos / (unsigned long)BytesPerLine;
    if (curLine < TopLine) TopLine = curLine;
    else if (curLine >= TopLine + (unsigned long)EdLines)
        TopLine = curLine - (unsigned long)EdLines + 1;
}

void DisplayHexEditor(void) {
    int row, col;
    unsigned long addr;
    unsigned char byte;
    int maxLines, botRow, gs;
    const char *modeStr;
    int hexWidth, hexContentCol, asciiBoxLeftCol, asciiContentCol;
    char statusBuf[320];

    _setcursortype(_NOCURSOR);

    ComputeEditorLayout();
    maxLines = EdLines;
    botRow   = 5 + maxLines;            /* box bottom border; ConH - 4 */
    gs       = (GroupSize >= 16) ? BYTES_PER_LINE : GroupSize;

    /* Box geometry - derived from the group size so the borders always
       land in the columns the hex/ascii content itself lines up in.
       hexWidth is the exact printed width of one row's hex column (2 hex
       digits per byte, plus one extra space at each group boundary except
       the last). */
    hexWidth        = BYTES_PER_LINE * 2 + (BYTES_PER_LINE / gs - 1);
    hexContentCol   = 2;
    asciiBoxLeftCol = 13 + hexWidth;
    asciiContentCol = 14 + hexWidth;

    /* Row 1: program banner */
    gotoxy(1, 1);
    textcolor(CLR_BANNER_FG);
    textbackground(CLR_BANNER_BG);

    modeStr = (CursorArea == AREA_HEX) ? "HEX" : "ASCII";

    cprintf(" EPROM Editor v1.6 - Mickey W. Lawless [%s MODE]%s", modeStr, Modified ? " *MOD*" : "     ");
    clreol();

    /* Row 2: PROMlink-style pulldown menu bar (Alt+letter opens each) */
    ComputeMenuBarCols();
    DrawTopMenuBar(-1);
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);

    /* Row 3: column header, aligned above the boxed hex/ascii columns. */
    gotoxy(1, 3);
    textcolor(CLR_FED_HDR);
    textbackground(BLACK);
    clreol();
    gotoxy(hexContentCol, 3);
    for (col = 0; col < BYTES_PER_LINE; col++) {
        cprintf("%X ", col & 0x0F);
        if ((col + 1) % gs == 0 && col != BYTES_PER_LINE - 1) cprintf(" ");
    }
    gotoxy(asciiContentCol, 3);
    for (col = 0; col < BYTES_PER_LINE; col++) cprintf("%X", col & 0x0F);

    /* Row 4: box top border - two separate boxes (hex, ascii), with the
       un-boxed offset column showing through the gap between them. */
    gotoxy(1, 4);
    textcolor(CLR_FED_BOX);
    textbackground(BLACK);
    clreol();
    gotoxy(1, 4);
    cprintf("\xDA");
    for (col = 0; col < hexWidth; col++) cprintf("\xC4");
    cprintf("\xBF");
    gotoxy(asciiBoxLeftCol, 4);
    cprintf("\xDA");
    for (col = 0; col < BYTES_PER_LINE; col++) cprintf("\xC4");
    cprintf("\xBF");

    /* Rows 5..botRow-1: boxed data grid - hex | offset | ascii. Always
       draws all maxLines rows (blank past BufferSize) so the box borders
       stay a continuous, unbroken rectangle. */
    for (row = 0; row < maxLines; row++) {
        addr = (TopLine + row) * BYTES_PER_LINE;
        gotoxy(1, 5 + row);

        textcolor(CLR_FED_BOX);
        textbackground(BLACK);
        cprintf("\xB3");

        textcolor(CLR_FED_DATA);
        for (col = 0; col < BYTES_PER_LINE; col++) {
            if (addr + col < BufferSize) {
                byte = DataBuffer[addr + col];
                if (addr + col == CursorPos) { textcolor(CLR_CURSOR_FG); textbackground(CLR_CURSOR_BG); }
                cprintf("%c%c", NibbleToHex(byte >> 4), NibbleToHex(byte & 0x0F));
                if (addr + col == CursorPos) { textcolor(CLR_FED_DATA); textbackground(BLACK); }
            } else {
                cprintf("  ");
            }
            if ((col + 1) % gs == 0 && col != BYTES_PER_LINE - 1) cprintf(" ");
        }

        textcolor(CLR_FED_BOX);
        cprintf("\xB3");

        textcolor(CLR_FED_DATA);
        cprintf("   ");
        if (addr < BufferSize) {
            textcolor(CLR_FED_OFFSET);
            cprintf("%04lX", addr);
            textcolor(CLR_FED_DATA);
        } else {
            cprintf("    ");
        }
        cprintf("   ");

        textcolor(CLR_FED_BOX);
        cprintf("\xB3");

        textcolor(CLR_FED_DATA);
        for (col = 0; col < BYTES_PER_LINE; col++) {
            if (addr + col < BufferSize) {
                byte = DataBuffer[addr + col];
                if (addr + col == CursorPos) { textcolor(CLR_CURSOR_FG); textbackground(CLR_CURSOR_BG); }
                cprintf("%c", (byte >= 32 && byte < 127) ? byte : '.');
                if (addr + col == CursorPos) { textcolor(CLR_FED_DATA); textbackground(BLACK); }
            } else {
                cprintf(" ");
            }
        }

        textcolor(CLR_FED_BOX);
        cprintf("\xB3");

        textcolor(CLR_FED_DATA);
        textbackground(BLACK);
        clreol();
    }

    /* Box bottom border, mirroring row 4. */
    gotoxy(1, botRow);
    textcolor(CLR_FED_BOX);
    textbackground(BLACK);
    clreol();
    gotoxy(1, botRow);
    cprintf("\xC0");
    for (col = 0; col < hexWidth; col++) cprintf("\xC4");
    cprintf("\xD9");
    gotoxy(asciiBoxLeftCol, botRow);
    cprintf("\xC0");
    for (col = 0; col < BYTES_PER_LINE; col++) cprintf("\xC4");
    cprintf("\xD9");

    /* Blank gap row between the box and the status bar. */
    gotoxy(1, botRow + 1);
    textcolor(CLR_FED_DATA);
    textbackground(BLACK);
    cprintf("%-*s", ConW, "");

    /* Solid teal status bar (full window width). */
    gotoxy(1, botRow + 2);
    textcolor(CLR_FED_SBAR_FG);
    textbackground(CLR_FED_SBAR_BG);
    if (BufferSize > 0 && CursorPos < BufferSize) {
        sprintf(statusBuf, " %-5s  Offset:%04lX  Value:%02X (%d)  Fill:%02X  Size:%lu",
                (CursorArea == AREA_HEX) ? "HEX" : "ASCII",
                CursorPos,
                DataBuffer[CursorPos],
                DataBuffer[CursorPos],
                FillByte,
                BufferSize);
        cprintf("%-*s", ConW, statusBuf);
    } else {
        cprintf("%-*s", ConW, " Empty Buffer");
    }

    /* Alt-toggled command hint bar */
    gotoxy(1, botRow + 3);
    if (ShowCommandHelp) {
        textcolor(CLR_MENUBAR_FG);
        textbackground(CLR_MENUBAR_BG);
        cprintf(" < ENTER=Edit U=Upload S=Save H=Hex L=List P=Print O=Open F=Find G=Goto ESC=Exit >");
    } else {
        textcolor(CLR_FED_DATA);
        textbackground(BLACK);
        cprintf("%-*s", ConW, "");
    }

    /* Last row: File / Copyright footer - width capped at ConW-1, never
       the full width (a full-width write on the last row triggers an
       auto-scroll that silently eats row 1). */
    gotoxy(1, ConH);
    textcolor(CLR_FED_FOOTER);
    textbackground(BLACK);
    cprintf("%-*s", ConW - 1, "");
    gotoxy(1, ConH);
    {
        char footerLeft[64];
        const char *footerRight = "Copyright 2025 Mickey W. Lawless";
        int rlen = (int)strlen(footerRight);
        if (CurrentFileName[0] != '\0')
            sprintf(footerLeft, "File: %.40s", CurrentFileName);
        else
            strcpy(footerLeft, "File: (none)");
        cprintf("%s", footerLeft);
        gotoxy(ConW - rlen, ConH);
        cprintf("%s", footerRight);
    }

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
    _setcursortype(_NORMALCURSOR);
}

/* OpenCategoryEditor - dropdown dispatch while inside the hex editor.
   Every action here calls the exact same functions the existing
   single-key hotkeys already call; this is just another way in.
   PulldownList returns a negative value when the user pressed a
   DIFFERENT category's Alt-key while this dropdown was open - in
   that case we switch straight to that category instead of stopping. */
void OpenCategoryEditor(int cat) {
    int r, nextCat;
    while (cat >= 0) {
        nextCat = -1;
        switch (cat) {
            case MB_SETUP: {
                static const char *items[] = {
                    ("Read from " PP_PORTNAME), "Select Programmer", "New Programmer",
                    "Edit Programmer", "Delete Programmer"
                };
                r = PulldownList(MenuBarCol[MB_SETUP], 3, " Setup ", items, 5);
                if (r < 0) { nextCat = -r - 1; }
                else if (r == 1) {
                    unsigned long baud = (ActiveProfile >= 0) ? Profiles[ActiveProfile].baud : 9600UL;
                    if (!InitCOM1(baud)) { DisplayHexEditor(); break; }
                    DisplayHexEditor();
                    if (ActiveProfile >= 0 && Profiles[ActiveProfile].initStr[0] != '\0') {
                        unsigned int k, ilen = (unsigned int)strlen(Profiles[ActiveProfile].initStr);
                        for (k = 0; k < ilen; k++) {
                            WriteCOM1((unsigned char)Profiles[ActiveProfile].initStr[k]);
                            delay(5);
                        }
                        WriteCOM1('\r');
                        delay(50);
                    } else {
                        SendInitString();
                    }
                    ReadDataFromCOM1();
                    if (BufferSize > 0) {
                        CursorPos = 0; TopLine = 0; CursorArea = AREA_HEX; Modified = 0;
                    }
                } else if (r == 2) SelectProgrammer();
                else if (r == 3) NewProgrammer();
                else if (r == 4) EditProgrammer();
                else if (r == 5) DeleteProgrammer();
                break;
            }
            case MB_DATA: {
                static const char *items[] = { "Open File", "Save Binary", "Save Intel HEX", "Save Listing" };
                r = PulldownList(MenuBarCol[MB_DATA], 3, " Data ", items, 4);
                if (r < 0) { nextCat = -r - 1; }
                else if (r == 1) {
                    LoadFileIntoBuffer();
                    if (BufferSize > 0) { CursorPos = 0; TopLine = 0; CursorArea = AREA_HEX; }
                } else if (r == 2) SaveBufferToFile();
                else if (r == 3) SaveBufferAsHex();
                else if (r == 4) SaveListing();
                break;
            }
            case MB_PROGRAM: {
                static const char *items[] = { "Upload to EPROM" };
                r = PulldownList(MenuBarCol[MB_PROGRAM], 3, " Program ", items, 1);
                if (r < 0) { nextCat = -r - 1; }
                else if (r == 1) UploadToEPROM();
                break;
            }
            case MB_TASKS: {
                static const char *items[] = { "Edit Byte", "Find Text", "Goto Address" };
                r = PulldownList(MenuBarCol[MB_TASKS], 3, " Tasks ", items, 3);
                if (r < 0) { nextCat = -r - 1; }
                else if (r == 1) {
                    if (CursorArea == AREA_HEX) EditByteHex();
                    else EditByteASCII();
                } else if (r == 2) FindInBuffer();
                else if (r == 3) GotoAddress();
                break;
            }
            case MB_UTILITIES: {
                static const char *items[] = { "Print", "Fill Range" };
                r = PulldownList(MenuBarCol[MB_UTILITIES], 3, " Utilities ", items, 2);
                if (r < 0) { nextCat = -r - 1; }
                else if (r == 1) PrintBuffer();
                else if (r == 2) FillRange();
                break;
            }
            case MB_OPTIONS: {
                static const char *items[] = { "Fill Byte Value", "Byte Grouping", PP_PORTNAME, "Window Size" };
                r = PulldownList(MenuBarCol[MB_OPTIONS], 3, " Options ", items, 4);
                if (r < 0) { nextCat = -r - 1; }
                else if (r == 1) SetFillByte();
                else if (r == 2) SetGroupSize();
                else if (r == 3) SetComPort();
                else if (r == 4) SetWindowSize();
                break;
            }
            case MB_EXIT: {
                if (Modified) {
                    if (DlgYesNo(" Exit ", "Buffer modified! Save before exit?"))
                        SaveBufferToFile();
                }
                RequestExit = 1;
                return;
            }
            case MB_HELP: {
                DlgMessage(" Help ", "EPROM Data Reader & Hex Editor v1.6",
                           "Alt+letter opens a menu. F11 = maximize.", 0);
                break;
            }
        }
        if (nextCat >= 0) DisplayHexEditor();
        cat = nextCat;
    }
    DisplayHexEditor();
}

void HexEditor(void) {
    int key;
#define maxLines EdLines   /* data rows on screen - set by DisplayHexEditor */
    int cursorCol;

    DisplayHexEditor();

    while (1) {
        if (conResized) { conResized = 0; DisplayHexEditor(); }
        if (Plat_AltHeld()) {
            if (!ShowCommandHelp) { ShowCommandHelp = 1; DisplayHexEditor(); }
        } else {
            if (ShowCommandHelp) { ShowCommandHelp = 0; DisplayHexEditor(); }
        }

        if (kbhit()) {
            if (conResized) { conResized = 0; DisplayHexEditor(); }
            key = getch();

            if (key == 0 || key == 224) {
                key = getch();
                switch (key) {
                    case SCAN_RESIZE: conResized = 0; DisplayHexEditor(); break;
                    case 72: case 56:
                        if (CursorPos >= BYTES_PER_LINE) {
                            CursorPos -= BYTES_PER_LINE;
                            if (CursorPos < TopLine * BYTES_PER_LINE) TopLine--;
                            DisplayHexEditor();
                        }
                        break;
                    case 80: case 50:
                        if (CursorPos + BYTES_PER_LINE < BufferSize) {
                            CursorPos += BYTES_PER_LINE;
                            if (CursorPos >= (TopLine + maxLines) * BYTES_PER_LINE) TopLine++;
                            CursorArea = AREA_HEX;
                            DisplayHexEditor();
                        }
                        break;
                    case 75: case 52:
                        cursorCol = CursorPos % BYTES_PER_LINE;
                        if (CursorArea == AREA_ASCII && cursorCol == 0) {
                            CursorArea = AREA_HEX;
                            CursorPos = (CursorPos / BYTES_PER_LINE) * BYTES_PER_LINE + (BYTES_PER_LINE - 1);
                            DisplayHexEditor();
                        } else if (CursorArea == AREA_ASCII && cursorCol > 0) {
                            CursorPos--; DisplayHexEditor();
                        } else if (CursorArea == AREA_HEX && CursorPos > 0) {
                            CursorPos--;
                            if (CursorPos < TopLine * BYTES_PER_LINE) TopLine--;
                            DisplayHexEditor();
                        }
                        break;
                    case 77: case 54:
                        cursorCol = CursorPos % BYTES_PER_LINE;
                        if (CursorArea == AREA_HEX && cursorCol == BYTES_PER_LINE - 1) {
                            CursorArea = AREA_ASCII;
                            CursorPos = (CursorPos / BYTES_PER_LINE) * BYTES_PER_LINE;
                            DisplayHexEditor();
                        } else if (CursorArea == AREA_HEX && cursorCol < BYTES_PER_LINE - 1) {
                            CursorPos++; DisplayHexEditor();
                        } else if (CursorArea == AREA_ASCII && cursorCol == BYTES_PER_LINE - 1) {
                            if (CursorPos + 1 < BufferSize) {
                                CursorPos++; CursorArea = AREA_HEX;
                                if (CursorPos >= (TopLine + maxLines) * BYTES_PER_LINE) TopLine++;
                                DisplayHexEditor();
                            }
                        } else if (CursorArea == AREA_ASCII && cursorCol < BYTES_PER_LINE - 1) {
                            CursorPos++; DisplayHexEditor();
                        }
                        break;
                    case 73:
                        if (TopLine > 0) {
                            if (TopLine < (unsigned long)maxLines) TopLine = 0;
                            else TopLine -= maxLines;
                            CursorPos = TopLine * BYTES_PER_LINE;
                            CursorArea = AREA_HEX;
                            DisplayHexEditor();
                        }
                        break;
                    case 81:
                        if ((TopLine + maxLines) * BYTES_PER_LINE < BufferSize) {
                            TopLine += maxLines;
                            CursorPos = TopLine * BYTES_PER_LINE;
                            CursorArea = AREA_HEX;
                            DisplayHexEditor();
                        }
                        break;
                    case ALTSCAN_S: OpenCategoryEditor(MB_SETUP); break;
                    case ALTSCAN_D: OpenCategoryEditor(MB_DATA); break;
                    case ALTSCAN_P: OpenCategoryEditor(MB_PROGRAM); break;
                    case ALTSCAN_T: OpenCategoryEditor(MB_TASKS); break;
                    case ALTSCAN_U: OpenCategoryEditor(MB_UTILITIES); break;
                    case ALTSCAN_O: OpenCategoryEditor(MB_OPTIONS); break;
                    case ALTSCAN_E: OpenCategoryEditor(MB_EXIT); break;
                    case ALTSCAN_H: OpenCategoryEditor(MB_HELP); break;
                    case SCAN_F1: OpenCategoryEditor(MB_HELP); break;
                }
            } else if (key == 27) {
                if (Modified) {
                    if (DlgYesNo(" Exit ", "Buffer modified! Save before exit?"))
                        SaveBufferToFile();
                }
                break;
            } else if (key == 'f' || key == 'F') {
                FindInBuffer(); DisplayHexEditor();
            } else if (key == 'g' || key == 'G') {
                GotoAddress(); DisplayHexEditor();
            } else if (key == 13) {
                if (CursorArea == AREA_HEX) EditByteHex();
                else EditByteASCII();
                DisplayHexEditor();
            } else if (key == 's' || key == 'S') {
                SaveBufferToFile(); DisplayHexEditor();
            } else if (key == 'h' || key == 'H') {
                SaveBufferAsHex(); DisplayHexEditor();
            } else if (key == 'l' || key == 'L') {
                SaveListing(); DisplayHexEditor();
            } else if (key == 'p' || key == 'P') {
                PrintBuffer(); DisplayHexEditor();
            } else if (key == 'u' || key == 'U') {
                UploadToEPROM(); DisplayHexEditor();
            } else if (key == 'o' || key == 'O') {
                LoadFileIntoBuffer();
                if (BufferSize > 0) { CursorPos = 0; TopLine = 0; CursorArea = AREA_HEX; }
                DisplayHexEditor();
            }
        } else {
            delay(10);
        }
        if (RequestExit) break;
    }
}
#undef maxLines

/* Main program */
static void DrawTitleScreen(void) {
    static const int letterSeq[8] = { 0, 1, 2, 3, -1, 0, 1, 2 }; /* P R O M _ P R O */
    static const char *font[4][7] = {
        { "#####", "#...#", "#...#", "#####", "#....", "#....", "#...." }, /* P */
        { "#####", "#...#", "#...#", "#####", "#.#..", "#..#.", "#...#" }, /* R */
        { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, /* O */
        { "#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#" }  /* M */
    };
    int row, i, c, startX, totalW, idx, vOff;
    const char *g;

    textmode(C80);
    clrscr();
    vOff = (SCREEN_H - 25) / 2;
    if (vOff < 0) vOff = 0;

    totalW = 7 * 6 + 3;
    startX = (SCREEN_W - totalW) / 2 + 1;

    textcolor(WHITE);
    textbackground(BLACK);

    for (row = 0; row < 7; row++) {
        gotoxy(startX, 5 + vOff + row);
        for (i = 0; i < 8; i++) {
            idx = letterSeq[i];
            if (idx < 0) {
                cprintf("   ");
            } else {
                g = font[idx][row];
                for (c = 0; c < 5; c++) cprintf((g[c] == '#') ? "\xDB" : " ");
                cprintf(" ");
            }
        }
    }

    textcolor(CLR_MENUBAR_HI);
    {
        const char *line1 = "EPROM Data Reader & Hex Editor v1.6";
        gotoxy((SCREEN_W - (int)strlen(line1)) / 2 + 1, 14 + vOff);
        cprintf("%s", line1);
    }
    textcolor(WHITE);
    {
        const char *line2 = "Copyright (c) 2025 Mickey W. Lawless";
        gotoxy((SCREEN_W - (int)strlen(line2)) / 2 + 1, 16 + vOff);
        cprintf("%s", line2);
    }
    textcolor(LIGHTGRAY);
    {
        const char *line3 = "All Rights Reserved";
        gotoxy((SCREEN_W - (int)strlen(line3)) / 2 + 1, 17 + vOff);
        cprintf("%s", line3);
    }

    textcolor(CLR_MENUBAR_HI);
    {
        const char *prompt = "[ Press ENTER to continue ]";
        gotoxy((SCREEN_W - (int)strlen(prompt)) / 2 + 1, 21 + vOff);
        cprintf("%s", prompt);
    }

    textcolor(LIGHTGRAY);
    textbackground(BLACK);

}

void ShowTitleScreen(void) {
    int ch;

    DrawTitleScreen();
    for (;;) {
        ch = getch();
        if (ch == 13) break;
        if ((ch == 0 || ch == 224) && getch() == SCAN_RESIZE) DrawTitleScreen();
    }
}

int main(int argc, char *argv[]) {
    int menuChoice;
    int ai;
    int comGiven = 0;
    const char *fileArg = NULL;
    FILE *fp;

    ConInit();
    InitConfigPaths(argv[0]);
    LoadComPort();
    for (ai = 1; ai < argc; ai++) {
        if (Plat_ParseArg(argc, argv, &ai)) comGiven = 1;
        else if (fileArg == NULL)
            fileArg = argv[ai];
    }
    /* No explicit port and the saved/default one isn't present: use the
       first serial port the system actually reports. */
    Plat_AutoSelectPort(comGiven);

    ShowTitleScreen();

    DataBuffer = PP_ALLOC(MAX_BUFFER_SIZE);
    if (DataBuffer == NULL) {
        textcolor(LIGHTRED);
        cprintf("ERROR: out of memory!\r\n");
        cprintf("Cannot allocate %lu byte buffer.\r\n", (unsigned long)MAX_BUFFER_SIZE);
        textcolor(LIGHTGRAY);
        cprintf("Press any key to exit...\r\n");
        getch();
        return 1;
    }

    LoadProfiles();

    if (fileArg != NULL && fileArg[0] != '\0') {
        char cmdFilename[MAX_PATH];
        long fileSize;
        strncpy(cmdFilename, fileArg, MAX_PATH - 1);
        cmdFilename[MAX_PATH - 1] = '\0';

        DisplayHexEditor();

        fp = fopen(cmdFilename, "rb");
        if (fp == NULL) {
            DlgMessage(" Error ", "Cannot open file:", cmdFilename, 1);
            PP_FREE(DataBuffer);
            ConExitScreen();
            textcolor(LIGHTGREEN);
            cprintf("EPROM Reader terminated.\r\n");
            textcolor(LIGHTGRAY);
            return 1;
        }

        fseek(fp, 0L, SEEK_END);
        fileSize = ftell(fp);
        fseek(fp, 0L, SEEK_SET);
        if (fileSize < 0) fileSize = 0;

        {
            int w = 60, h = 10, r, barX, filled, i;
            char line[64];
            unsigned char nearBuf[512];
            int bytesRead;
            unsigned int j, chunk;
            unsigned long total = (unsigned long)fileSize;

            r = DlgDraw(w, h, " Loading File ");
            sprintf(line, "File: %.40s", cmdFilename);
            DlgLineLeft(r,     w, line);
            sprintf(line, "Size: %lu bytes", total);
            DlgLineLeft(r + 1, w, line);

            barX = (SCREEN_W - w) / 2 + 2;

            BufferSize = 0;
            while (BufferSize < MAX_BUFFER_SIZE) {
                chunk = (MAX_BUFFER_SIZE - BufferSize > 512UL)
                        ? 512 : (unsigned int)(MAX_BUFFER_SIZE - BufferSize);
                bytesRead = fread(nearBuf, 1, chunk, fp);
                if (bytesRead <= 0) break;
                for (j = 0; j < (unsigned int)bytesRead; j++)
                    DataBuffer[BufferSize + j] = nearBuf[j];
                BufferSize += (unsigned int)bytesRead;

                gotoxy(barX, r + 3);
                textcolor(CLR_DLG_OK);
                textbackground(CLR_WIN_BG);
                filled = (total > 0)
                         ? (int)(((unsigned long)BufferSize * (unsigned long)UPLOAD_BAR_WIDTH) / total)
                         : UPLOAD_BAR_WIDTH;
                if (filled > UPLOAD_BAR_WIDTH) filled = UPLOAD_BAR_WIDTH;
                for (i = 0; i < filled; i++) cprintf("\xDB");
                textcolor(CLR_DLG_BORDER);
                for (; i < UPLOAD_BAR_WIDTH; i++) cprintf("\xB0");

                sprintf(line, "%lu / %lu bytes", BufferSize, total);
                gotoxy(barX, r + 4);
                textcolor(CLR_WIN_FG);
                textbackground(CLR_WIN_BG);
                cprintf("%-50s", line);
            }
            fclose(fp);
            delay(400);
        }

        if (BufferSize == 0) {
            DlgMessage(" Error ", "File is empty or could not be read.", "", 1);
            PP_FREE(DataBuffer);
            ConExitScreen();
            textcolor(LIGHTGREEN);
            cprintf("EPROM Reader terminated.\r\n");
            textcolor(LIGHTGRAY);
            return 1;
        }

        strncpy(CurrentFileName, cmdFilename, 79);
        CurrentFileName[79] = '\0';

        CursorPos = 0; TopLine = 0; EditMode = MODE_VIEW;
        CursorArea = AREA_HEX; Modified = 0;

        HexEditor();
        CloseCOM1();
        PP_FREE(DataBuffer);
        return 0;
    }

restart_menu:
    menuChoice = ShowStartupMenu();

    if (menuChoice == 4) {
        textcolor(LIGHTGRAY);
        cprintf("\r\nExiting...\r\n");
        CloseCOM1();
        PP_FREE(DataBuffer);
        return 0;
    }

    if (menuChoice == 1) {
        unsigned long baud = (ActiveProfile >= 0) ? Profiles[ActiveProfile].baud : 9600UL;
        char lbl[40];
        if (!InitCOM1(baud)) goto restart_menu;
        ComPortLabel(lbl, (int)sizeof(lbl));
        DisplayHexEditor();
        if (ActiveProfile >= 0 && Profiles[ActiveProfile].initStr[0] != '\0') {
            unsigned int k, ilen = (unsigned int)strlen(Profiles[ActiveProfile].initStr);
            for (k = 0; k < ilen; k++) {
                WriteCOM1((unsigned char)Profiles[ActiveProfile].initStr[k]);
                delay(5);
            }
            WriteCOM1('\r');
            delay(50);
        } else {
            SendInitString();
        }
        DisplayHexEditor();
        {
            int w = 50, h = 8, r;
            char line[64];
            r = DlgDraw(w, h, " Read from Programmer ");
            sprintf(line, "%.24s initialized at %lu baud.", lbl, baud);
            DlgLine(r,     w, line);
            DlgLine(r + 3, w, "[ Press any key to begin reading ]");
            getch();
        }
        ReadDataFromCOM1();
        if (BufferSize == 0) {
            CloseCOM1();
            clrscr();
            textcolor(LIGHTRED);
            cprintf("\r\nERROR: No data received from %s!\r\n\r\n", lbl);
            textcolor(LIGHTGRAY);
            cprintf("Check:\r\n");
            cprintf("  - %s connection and cable\r\n", lbl);
            cprintf("  - Baud rate (currently %lu)\r\n", baud);
            cprintf("  - Data format: ASCII space-separated hex\r\n");
            cprintf("  - Device is transmitting\r\n\r\n");
            cprintf("Press any key to exit...\r\n");
            getch();
            PP_FREE(DataBuffer);
            return 1;
        }
    } else if (menuChoice == 2) {
        LoadFileIntoBuffer();
        if (BufferSize == 0) {
            textcolor(LIGHTRED);
            cprintf("\r\n\r\nNo file loaded. Exiting...\r\n");
            textcolor(LIGHTGRAY);
            CloseCOM1();
            PP_FREE(DataBuffer);
            return 1;
        }
    } else if (menuChoice == 3) {
        BufferSize = 0;
        textcolor(LIGHTGREEN);
        cprintf("\r\nEntering empty editor...\r\n");
        cprintf("Use 'L' key in editor to load a file.\r\n");
        cprintf("Press any key to continue...");
        getch();
    }

    if (menuChoice == 3) {
        DataBuffer[0] = 0x00;
        BufferSize = 1;
    }

    CursorPos = 0; TopLine = 0; EditMode = MODE_VIEW;
    Modified = 0; CursorArea = AREA_HEX;

    HexEditor();

    CloseCOM1();
    PP_FREE(DataBuffer);

    ConExitScreen();
    textcolor(LIGHTGREEN);
    cprintf("EPROM Reader terminated.\r\n");
    textcolor(LIGHTGRAY);

    return 0;
}

#endif /* PP_IN(2) */

#if PP_IN(3)
void SendInitString(void) {
    char initStr[41];
    unsigned int k, len;
    int w = 54, h = 9, r;

    initStr[0] = '\0';
    r = DlgDraw(w, h, " Programmer Init String ");
    DlgLine(r,     w, "Enter a command to send (or blank):");
    DlgLine(r + 1, w, "e.g. R or READ  -  ESC to skip");
    if (!DlgInput(r + 3, w, "Command:", initStr, 40) || initStr[0] == '\0') return;

    len = (unsigned int)strlen(initStr);
    for (k = 0; k < len; k++) {
        WriteCOM1((unsigned char)initStr[k]);
        delay(5);
    }
    WriteCOM1('\r');
    delay(50);
}

/*
 * UploadToEPROM - complete replacement for RRV / EPROM Reader v1.3
 *
 * Supports all 16 EZ-WRITER MULTIPROGRAMMER data-translation formats:
 *
 *   fmt  1  Standard Binary          (raw bytes)
 *   fmt  2  BYTEK Binary Code 20     (count + addr + data + sumcheck)
 *   fmt  3  DATA I/O Binary Code 10  (arrow header + byte-count + rubout + data + nulls + sumcheck)
 *   fmt  4  DEC Binary Code 11       (rubout leader + null + raw binary)
 *   fmt  5  Motorola Exorciser S1    Code 82  (Motorola S-Record S1)
 *   fmt  6  ASCII Space HEX          Code 50
 *   fmt  7  ASCII HEX % STX          Code 51
 *   fmt  8  ASCII HEX % SOH          Code 56
 *   fmt  9  Fairchild F8             Code 80
 *   fmt 10  MOSTEK                   Code 81
 *   fmt 11  Intel MDS 8Bit           Code 83  (Intel HEX)
 *   fmt 12  Signetics                Code 85
 *   fmt 13  Tektronix Hexadecimal    Code 86
 *   fmt 14  Motorola Exormax S2      Code 87
 *   fmt 15  Intel MCS-86 HEX         Code 88  (Intel HEX + extended address)
 *   fmt 16  Motorola 32Bit S3        Code 95
 *
 * Compiler : MinGW-w64 gcc / MSVC (C89)
 * Platform : Windows console
 * Author   : Mickey W. Lawless
 */

/* -----------------------------------------------------------------------
   Helper: send a NUL-terminated ASCII string over COM1
   ----------------------------------------------------------------------- */
void SendStr(const char *s) {
    unsigned int k;
    for (k = 0; s[k] != '\0'; k++) {
        WriteCOM1((unsigned char)s[k]);
    }
}

/* -----------------------------------------------------------------------
   Helper: send 'count' copies of byte 'val'
   ----------------------------------------------------------------------- */
void SendRepeat(unsigned char val, unsigned int count) {
    unsigned int k;
    for (k = 0; k < count; k++) WriteCOM1(val);
}

/* -----------------------------------------------------------------------
   Helper: send 2-hex-digit ASCII representation of one byte
   ----------------------------------------------------------------------- */
void SendHexByte(unsigned char b) {
    WriteCOM1((unsigned char)NibbleToHex(b >> 4));
    WriteCOM1((unsigned char)NibbleToHex(b & 0x0F));
}

/* -----------------------------------------------------------------------
   Helper: send 4-hex-digit ASCII representation of a 16-bit value
   ----------------------------------------------------------------------- */
void SendHexWord(unsigned int w) {
    WriteCOM1((unsigned char)NibbleToHex((w >> 12) & 0x0F));
    WriteCOM1((unsigned char)NibbleToHex((w >>  8) & 0x0F));
    WriteCOM1((unsigned char)NibbleToHex((w >>  4) & 0x0F));
    WriteCOM1((unsigned char)NibbleToHex( w        & 0x0F));
}

/* -----------------------------------------------------------------------
   Helper: send 8-hex-digit ASCII representation of an unsigned long
   ----------------------------------------------------------------------- */
void SendHexLong(unsigned long v) {
    SendHexWord((unsigned int)((v >> 16) & 0xFFFFUL));
    SendHexWord((unsigned int)( v        & 0xFFFFUL));
}

/* -----------------------------------------------------------------------
   Helper: print progress dot on ESC-check cadence; returns 1 if aborted
   ----------------------------------------------------------------------- */
/* -----------------------------------------------------------------------
   Helper: polls for ESC (returns 1 if aborted) and, on every poll,
   refreshes the upload dialog's block-graphics progress bar.
   ----------------------------------------------------------------------- */
int CheckAbort(unsigned long done, unsigned long total) {
    if (kbhit() && getch() == 27) {
        ComPurgeTx();
        DlgLineLeft(UploadDlgRow + 6, UploadDlgW, "Cancelled.                    ");
        delay(500);
        return 1;
    }
    UpdateUploadBar(done, total);
    return 0;
}

/* -----------------------------------------------------------------------
   Upload progress dialog - drawn once per upload, then the bar and byte
   counter are refreshed in place as CheckAbort() polls during sending.
   ----------------------------------------------------------------------- */
void DrawUploadDialog(const char *fmtDesc, unsigned long total) {
    int r;
    char line[64];

    UploadTotal = total;
    r = DlgDraw(UploadDlgW, 11, " Uploading to Programmer ");
    UploadDlgRow = r;

    sprintf(line, "Source: Current buffer (%lu bytes)", total);
    DlgLineLeft(r,     UploadDlgW, line);
    sprintf(line, "Format: %s", fmtDesc);
    DlgLineLeft(r + 1, UploadDlgW, line);
    DlgLineLeft(r + 6, UploadDlgW, "Press ESC to cancel");

    UpdateUploadBar(0, total);
}

void UpdateUploadBar(unsigned long done, unsigned long total) {
    int i, filled, barX;
    unsigned int pct;
    char stat[64];

    pct = (total > 0) ? (unsigned int)((done * 100UL) / total) : 100;
    filled = (total > 0) ? (int)((done * (unsigned long)UPLOAD_BAR_WIDTH) / total) : UPLOAD_BAR_WIDTH;
    if (filled > UPLOAD_BAR_WIDTH) filled = UPLOAD_BAR_WIDTH;

    barX = (SCREEN_W - UploadDlgW) / 2 + 2;

    gotoxy(barX, UploadDlgRow + 3);
    textcolor(CLR_DLG_OK);
    textbackground(CLR_WIN_BG);
    for (i = 0; i < filled; i++) cprintf("\xDB");
    textcolor(CLR_DLG_BORDER);
    for (; i < UPLOAD_BAR_WIDTH; i++) cprintf("\xB0");

    sprintf(stat, "%lu / %lu bytes  (%u%%)", done, total, pct);
    gotoxy(barX, UploadDlgRow + 4);
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
    cprintf("%-50s", stat);

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

/* -----------------------------------------------------------------------
   Main upload function
   ----------------------------------------------------------------------- */
void UploadToEPROM(void) {
    /* --- variables used across all format branches --- */
    int  fmt;
    unsigned long addr;       /* BYTEK start address */
    unsigned long byteCount;
    unsigned long sent;
    unsigned long laddr;      /* current record base address (long) */
    unsigned int  recLen;     /* bytes per record */
    unsigned int  sc;         /* sumcheck accumulator */
    unsigned int  j;
    unsigned char byte;
    char srbuf[120];          /* scratch ASCII record buffer */
    int  slen;
    int  i;

    if (BufferSize == 0) {
        DlgMessage(" Upload to Programmer ", "Buffer is empty - nothing to upload.",
                   "Load or enter data first.", 0);
        return;
    }

    if (!InitCOM1(9600)) return;

    /* ---- Format selection dialog ---- */
    {
        static const char *fmtItems[] = {
            " 1. Standard Binary          (raw bytes)",
            " 2. BYTEK Binary    Code 20  (cnt+addr+data+sum)",
            " 3. DATA I/O Binary Code 10  (arrow+cnt+data+sum)",
            " 4. DEC Binary      Code 11  (rubout+null+binary)",
            " 5. Motorola S1     Code 82  (S-Record S1)",
            " 6. ASCII Space HEX Code 50",
            " 7. ASCII HEX % STX Code 51",
            " 8. ASCII HEX % SOH Code 56",
            " 9. Fairchild F8    Code 80",
            "10. MOSTEK          Code 81",
            "11. Intel MDS 8Bit  Code 83  (Intel HEX)",
            "12. Signetics       Code 85",
            "13. Tektronix HEX   Code 86",
            "14. Motorola S2     Code 87  (Exormax)",
            "15. Intel MCS-86    Code 88  (Intel HEX ext)",
            "16. Motorola S3     Code 95  (32-bit addr)"
        };
        fmt = DlgMenu(" Select Upload Format ", fmtItems, 16);
        if (!fmt) { DisplayHexEditor(); return; }
    }

    /* ---- Start address dialog for BYTEK ---- */
    addr = 0;
    if (fmt == 2) {
        char abuf[16];
        int w = 54, h = 9, r;
        abuf[0] = '\0';
        r = DlgDraw(w, h, " BYTEK Start Address ");
        DlgLine(r,     w, "Enter start address (hex):");
        DlgLine(r + 1, w, "e.g. 0000");
        DlgInput(r + 3, w, "Addr:", abuf, 6);
        if (abuf[0]) sscanf(abuf, "%lx", &addr);
    }

    byteCount = BufferSize;

    {
        char fmtDesc[48];
        switch (fmt) {
            case  1: strcpy(fmtDesc, "Standard Binary"); break;
            case  2: sprintf(fmtDesc, "BYTEK Code 20  Addr:%06lX", addr); break;
            case  3: strcpy(fmtDesc, "DATA I/O Binary Code 10"); break;
            case  4: strcpy(fmtDesc, "DEC Binary Code 11"); break;
            case  5: strcpy(fmtDesc, "Motorola Exorciser S1  Code 82"); break;
            case  6: strcpy(fmtDesc, "ASCII Space HEX  Code 50"); break;
            case  7: strcpy(fmtDesc, "ASCII HEX %STX  Code 51"); break;
            case  8: strcpy(fmtDesc, "ASCII HEX %SOH  Code 56"); break;
            case  9: strcpy(fmtDesc, "Fairchild F8  Code 80"); break;
            case 10: strcpy(fmtDesc, "MOSTEK  Code 81"); break;
            case 11: strcpy(fmtDesc, "Intel MDS 8Bit  Code 83"); break;
            case 12: strcpy(fmtDesc, "Signetics  Code 85"); break;
            case 13: strcpy(fmtDesc, "Tektronix Hexadecimal  Code 86"); break;
            case 14: strcpy(fmtDesc, "Motorola Exormax S2  Code 87"); break;
            case 15: strcpy(fmtDesc, "Intel MCS-86 HEX  Code 88"); break;
            case 16: strcpy(fmtDesc, "Motorola 32Bit S3  Code 95"); break;
            default: fmtDesc[0] = '\0';
        }
        DrawUploadDialog(fmtDesc, byteCount);
    }

    /* ==================================================================
       FORMAT 1: Standard Binary - raw bytes
       ================================================================== */
    if (fmt == 1) {
        sent = 0;
        while (sent < byteCount) {
            if (CheckAbort(sent, byteCount)) return;
            WriteCOM1(DataBuffer[sent]);
            sent++;
            delay(2);
        }
    }

    /* ==================================================================
       FORMAT 2: BYTEK Binary Code 20
       Header : count_HIGH count_LOW  addr_HIGH addr_MID addr_LOW
       Data   : raw bytes
       Footer : sumcheck_HIGH sumcheck_LOW  0x00 0x00
       ================================================================== */
    else if (fmt == 2) {
        unsigned int cnt = (unsigned int)(byteCount & 0xFFFFUL);
        sc = 0;
        sent = 0;

        WriteCOM1((unsigned char)((cnt >> 8) & 0xFF));
        WriteCOM1((unsigned char)( cnt        & 0xFF));
        WriteCOM1((unsigned char)((addr >> 16) & 0xFF));
        WriteCOM1((unsigned char)((addr >>  8) & 0xFF));
        WriteCOM1((unsigned char)( addr         & 0xFF));
        delay(10);

        while (sent < byteCount) {
            if (CheckAbort(sent, byteCount)) return;
            byte = DataBuffer[sent];
            WriteCOM1(byte);
            sc = (unsigned int)((sc + byte) & 0xFFFFU);
            sent++;
            delay(2);
        }

        WriteCOM1((unsigned char)((sc >> 8) & 0xFF));
        WriteCOM1((unsigned char)( sc        & 0xFF));
        WriteCOM1(0x00);
        WriteCOM1(0x00);
    }

    /* ==================================================================
       FORMAT 3: DATA I/O Binary Code 10
       Leader : 0x01 0x00 0x00 0x00 0x17  (arrow header, 5 bytes)
                0x00                      (1 null)
                [4 ASCII hex digits of byte count]
                0xFF                      (rubout / start code)
       Data   : raw binary bytes
       Footer : 0x00 0x00
                [4 ASCII hex digits of 16-bit sumcheck]
       ================================================================== */
    else if (fmt == 3) {
        unsigned int cnt = (unsigned int)(byteCount & 0xFFFFUL);
        char hexbuf[5];
        sc = 0;
        sent = 0;

        WriteCOM1(0x01); WriteCOM1(0x00); WriteCOM1(0x00);
        WriteCOM1(0x00); WriteCOM1(0x17);
        WriteCOM1(0x00);
        sprintf(hexbuf, "%04X", cnt);
        WriteCOM1((unsigned char)hexbuf[0]);
        WriteCOM1((unsigned char)hexbuf[1]);
        WriteCOM1((unsigned char)hexbuf[2]);
        WriteCOM1((unsigned char)hexbuf[3]);
        WriteCOM1(0xFF);
        delay(10);

        while (sent < byteCount) {
            if (CheckAbort(sent, byteCount)) return;
            byte = DataBuffer[sent];
            WriteCOM1(byte);
            sc = (unsigned int)((sc + byte) & 0xFFFFU);
            sent++;
            delay(2);
        }

        WriteCOM1(0x00); WriteCOM1(0x00);
        sprintf(hexbuf, "%04X", sc);
        WriteCOM1((unsigned char)hexbuf[0]);
        WriteCOM1((unsigned char)hexbuf[1]);
        WriteCOM1((unsigned char)hexbuf[2]);
        WriteCOM1((unsigned char)hexbuf[3]);
    }

    /* ==================================================================
       FORMAT 4: DEC Binary Code 11
       32 rubouts (0xFF) leader, 1 null (0x00), raw binary data
       ================================================================== */
    else if (fmt == 4) {
        sent = 0;
        SendRepeat(0xFF, 32);
        WriteCOM1(0x00);
        delay(10);

        while (sent < byteCount) {
            if (CheckAbort(sent, byteCount)) return;
            byte = DataBuffer[sent];
            WriteCOM1(byte);
            sent++;
            delay(2);
        }
    }

    /* ==================================================================
       FORMAT 5: Motorola Exorciser (S1) Code 82
       S0 header, S1 data records (16 bytes each), S9 end record.
       Record: S1 BC AAAA HH...HH SS
         BC   = byte_count = recLen + 3  (data + 2 addr + 1 checksum)
         AAAA = 16-bit address (4 hex chars)
         HH   = data bytes (2 hex chars each)
         SS   = one's complement of (BC + addr_hi + addr_lo + all_data_bytes) & 0xFF
       ================================================================== */
    else if (fmt == 5) {
        /* S0 header record */
        {
            const char *hdr = "HDR";
            unsigned int hcs = 0;
            unsigned int hlen = (unsigned int)(strlen(hdr) + 3);
            hcs = hlen + 0x00 + 0x00;
            for (j = 0; j < (unsigned int)strlen(hdr); j++)
                hcs += (unsigned char)hdr[j];
            hcs = (~hcs) & 0xFF;
            slen = sprintf(srbuf, "S0%02X0000%s%02X\r\n", hlen, hdr, hcs);
            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            delay(20);
        }

        laddr = 0;
        while (laddr < byteCount) {
            if (CheckAbort(laddr, byteCount)) return;
            recLen = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);
            sc  = recLen + 3;
            sc += (unsigned int)((laddr >> 8) & 0xFF);
            sc += (unsigned int)( laddr        & 0xFF);

            slen = sprintf(srbuf, "S1%02X%04lX", (unsigned int)(recLen + 3), laddr);
            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                sc  += byte;
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            sc   = (~sc) & 0xFF;
            slen += sprintf(srbuf + slen, "%02X\r\n", sc);
            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            laddr += recLen;
            delay(5);
        }

        /* S9 end record */
        slen = sprintf(srbuf, "S9030000FC\r\n");
        for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
            WriteCOM1((unsigned char)srbuf[j]);
    }

    /* ==================================================================
       FORMAT 6: ASCII Space HEX Code 50
       Data bytes sent as ASCII hex pairs separated by spaces.
       Lines of up to 16 bytes, each line terminated CR LF.
       Entire transmission terminated by Ctrl-C (0x03).
       The start address is entered at the programmer OFFSET prompt
       beforehand - it is not encoded in the data stream.
       ================================================================== */
    else if (fmt == 6) {
        sent = 0;
        while (sent < byteCount) {
            if (CheckAbort(sent, byteCount)) return;
            byte = DataBuffer[sent];
            /* send "HH " (hex pair + space) */
            WriteCOM1((unsigned char)NibbleToHex(byte >> 4));
            WriteCOM1((unsigned char)NibbleToHex(byte & 0x0F));
            sent++;
            /* end of line every 16 bytes, or at end of data */
            if ((sent % 16) == 0 || sent == byteCount) {
                WriteCOM1('\r');
                WriteCOM1('\n');
            } else {
                WriteCOM1(' ');
            }
            delay(2);
        }
        /* terminate with Ctrl-C */
        WriteCOM1(0x03);
    }

    /* ==================================================================
       FORMAT 7: ASCII HEX % STX Code 51
       Surrounded by 50 nulls before and after.
       Start code  : STX (0x02)
       Data bytes  : HH% for each byte (2 hex chars + '%' execute char)
       End code    : ETX (0x03)
       Sumcheck    : $SHHHH,  (4 hex digits sandwiched between "$S" and ",")
       Addresses are implied sequential; no address field sent.
       ================================================================== */
    else if (fmt == 7) {
        sc = 0;
        sent = 0;

        SendRepeat(0x00, 50);           /* 50 leading nulls */
        WriteCOM1(0x02);                /* STX start code */
        delay(10);

        while (sent < byteCount) {
            if (CheckAbort(sent, byteCount)) return;
            byte = DataBuffer[sent];
            sc = (unsigned int)((sc + byte) & 0xFFFFU);
            WriteCOM1((unsigned char)NibbleToHex(byte >> 4));
            WriteCOM1((unsigned char)NibbleToHex(byte & 0x0F));
            WriteCOM1('%');             /* execute character */
            sent++;
            delay(2);
        }

        WriteCOM1(0x03);                /* ETX end code */

        /* sumcheck field: $SHHHH, */
        WriteCOM1('$');
        WriteCOM1('S');
        SendHexWord(sc);
        WriteCOM1(',');

        SendRepeat(0x00, 50);           /* 50 trailing nulls */
    }

    /* ==================================================================
       FORMAT 8: ASCII HEX % SOH Code 56
       Identical to Code 51 (fmt 7) except start code is SOH (0x01)
       instead of STX (0x02).  Everything else - nulls, %, ETX,
       sumcheck field - is the same.
       ================================================================== */
    else if (fmt == 8) {
        sc = 0;
        sent = 0;

        SendRepeat(0x00, 50);
        WriteCOM1(0x01);                /* SOH start code */
        delay(10);

        while (sent < byteCount) {
            if (CheckAbort(sent, byteCount)) return;
            byte = DataBuffer[sent];
            sc = (unsigned int)((sc + byte) & 0xFFFFU);
            WriteCOM1((unsigned char)NibbleToHex(byte >> 4));
            WriteCOM1((unsigned char)NibbleToHex(byte & 0x0F));
            WriteCOM1('%');
            sent++;
            delay(2);
        }

        WriteCOM1(0x03);
        WriteCOM1('$'); WriteCOM1('S');
        SendHexWord(sc);
        WriteCOM1(',');
        SendRepeat(0x00, 50);
    }

    /* ==================================================================
       FORMAT 9: Fairchild F8 Code 80
       Start of file: S[4-hex-char address]  e.g. S0000
       Data records : X[up to 8 bytes as 2-hex-char pairs][1-hex checksum]
         Checksum = low nibble of (sum of all 16 hex nibble values in data field)
         i.e. sum each nibble as a number 0-F, keep low 4 bits.
       End of file  : *
       ================================================================== */
    else if (fmt == 9) {
        unsigned int  cksum;
        unsigned int  ni;

        /* start-of-file record: S followed by 4-hex start address (always 0000) */
        WriteCOM1('S');
        SendHexWord(0x0000);
        WriteCOM1('\r'); WriteCOM1('\n');
        delay(10);

        laddr = 0;
        while (laddr < byteCount) {
            if (CheckAbort(laddr, byteCount)) return;

            /* up to 8 bytes per record */
            recLen = (byteCount - laddr >= 8UL) ? 8 : (unsigned int)(byteCount - laddr);

            WriteCOM1('X');             /* data record start character */

            cksum = 0;
            for (j = 0; j < recLen; j++) {
                unsigned char hi_nib, lo_nib;
                byte    = DataBuffer[laddr + j];
                hi_nib  = byte >> 4;
                lo_nib  = byte & 0x0F;
                cksum  += hi_nib;
                cksum  += lo_nib;
                WriteCOM1((unsigned char)NibbleToHex(hi_nib));
                WriteCOM1((unsigned char)NibbleToHex(lo_nib));
            }

            /* checksum = low nibble of nibble sum */
            cksum &= 0x0FU;
            WriteCOM1((unsigned char)NibbleToHex((unsigned char)cksum));
            WriteCOM1('\r'); WriteCOM1('\n');

            laddr += recLen;
            delay(5);
        }

        /* end-of-file record */
        WriteCOM1('*');
        WriteCOM1('\r'); WriteCOM1('\n');
    }

    /* ==================================================================
       FORMAT 10: MOSTEK Code 81
       Each record: ; [2-hex byte count] [4-hex address] [data bytes] [4-hex checksum]
         Byte count = number of data bytes in record (hex, 2 chars)
         Address    = 4 hex chars
         Checksum   = 16-bit sum of (bytecount_byte + addr_hi + addr_lo + all_data_bytes) & 0xFFFF
       End record : ; [2-hex count = 00] [4-hex record count] [4-hex checksum]
         where record count = total number of data records written
       ================================================================== */
    else if (fmt == 10) {
        unsigned int  recCount = 0;

        laddr = 0;
        while (laddr < byteCount) {
            if (CheckAbort(laddr, byteCount)) return;
            recLen = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);

            /* checksum over: byte_count, addr_hi, addr_lo, all data bytes */
            sc  = (unsigned int)(recLen & 0xFF);
            sc += (unsigned int)((laddr >> 8) & 0xFF);
            sc += (unsigned int)( laddr        & 0xFF);
            for (j = 0; j < recLen; j++)
                sc += DataBuffer[laddr + j];
            sc &= 0xFFFFU;

            slen = sprintf(srbuf, ";%02X%04lX",
                           (unsigned int)recLen,
                           laddr);
            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            slen += sprintf(srbuf + slen, "%04X\r\n", sc);

            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);

            recCount++;
            laddr += recLen;
            delay(5);
        }

        /* end record: ;00[4-hex record count][4-hex checksum] */
        {
            unsigned int ecs;
            ecs  = 0x00;                        /* byte count (00) */
            ecs += (unsigned int)((recCount >> 8) & 0xFF);
            ecs += (unsigned int)( recCount        & 0xFF);
            ecs &= 0xFFFFU;
            slen = sprintf(srbuf, ";00%04X%04X\r\n",
                           (unsigned int)recCount, ecs);
            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
        }
    }

    /* ==================================================================
       FORMAT 11: Intel MDS 8Bit Code 83  (standard Intel HEX)
       : [2-hex byte count] [4-hex address] [2-hex rec type=00] [data] [2-hex checksum]
       Checksum = two's complement of (bytecount + addr_hi + addr_lo + rec_type + all_data) & 0xFF
       End: :00000001FF
       ================================================================== */
    else if (fmt == 11) {
        laddr = 0;
        while (laddr < byteCount) {
            if (CheckAbort(laddr, byteCount)) return;
            recLen = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);

            sc  = recLen;
            sc += (unsigned int)((laddr >> 8) & 0xFF);
            sc += (unsigned int)( laddr        & 0xFF);
            sc += 0x00;                 /* record type 00 */

            slen = sprintf(srbuf, ":%02X%04lX00", (unsigned int)recLen, laddr);
            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                sc  += byte;
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            sc   = (~sc + 1) & 0xFF;   /* two's complement */
            slen += sprintf(srbuf + slen, "%02X\r\n", sc);

            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            laddr += recLen;
            delay(5);
        }

        /* end-of-file record */
        SendStr(":00000001FF\r\n");
    }

    /* ==================================================================
       FORMAT 12: Signetics Code 85
       Record: : [4-hex addr] [2-hex byte count] [2-hex addr check] [data] [2-hex data check]
         addr check = (addr_hi + addr_lo) & 0xFF
         data check = sum of all data bytes & 0xFF
       End record: : [4-hex addr] [2-hex byte count = 00]
         (no data, no checksums in end record - just addr + 00)
       ================================================================== */
    else if (fmt == 12) {
        unsigned char addrCheck, dataCheck;
        laddr = 0;

        while (laddr < byteCount) {
            if (CheckAbort(laddr, byteCount)) return;
            recLen = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);

            addrCheck = (unsigned char)(
                          ((laddr >> 8) & 0xFF) +
                          ( laddr        & 0xFF) );
            dataCheck = 0;
            for (j = 0; j < recLen; j++)
                dataCheck += DataBuffer[laddr + j];

            slen = sprintf(srbuf, ":%04lX%02X%02X",
                           laddr,
                           (unsigned int)recLen,
                           (unsigned int)addrCheck);
            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            slen += sprintf(srbuf + slen, "%02X\r\n", (unsigned int)dataCheck);

            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            laddr += recLen;
            delay(5);
        }

        /* end record: :AAAA00  (last address, byte count 00, no data or checksum) */
        slen = sprintf(srbuf, ":%04lX00\r\n", laddr);
        for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
            WriteCOM1((unsigned char)srbuf[j]);
    }

    /* ==================================================================
       FORMAT 13: Tektronix Hexadecimal Code 86
       Record: / [4-hex addr] [2-hex byte count] [2-hex header checksum] [data bytes] [2-hex data checksum]
         header checksum = (addr_hi + addr_lo + byte_count) & 0xFF
         data checksum   = sum of all data bytes & 0xFF
       End record: / [4-hex transfer addr] 00 [2-hex checksum of addr+00]
       ================================================================== */
    else if (fmt == 13) {
        unsigned char hdrCs, datCs;
        laddr = 0;

        while (laddr < byteCount) {
            if (CheckAbort(laddr, byteCount)) return;
            recLen = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);

            hdrCs = (unsigned char)(
                        ((laddr >> 8) & 0xFF) +
                        ( laddr        & 0xFF) +
                        recLen );
            datCs = 0;
            for (j = 0; j < recLen; j++)
                datCs += DataBuffer[laddr + j];

            slen = sprintf(srbuf, "/%04lX%02X%02X",
                           laddr,
                           (unsigned int)recLen,
                           (unsigned int)hdrCs);
            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            slen += sprintf(srbuf + slen, "%02X\r\n", (unsigned int)datCs);

            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            laddr += recLen;
            delay(5);
        }

        /* end-of-file record: /AAAA00CC  (byte count = 00, checksum of addr+00) */
        {
            unsigned char eofCs;
            eofCs = (unsigned char)(
                        ((laddr >> 8) & 0xFF) +
                        ( laddr        & 0xFF) +
                        0x00 );
            slen = sprintf(srbuf, "/%04lX00%02X\r\n",
                           laddr, (unsigned int)eofCs);
            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
        }
    }

    /* ==================================================================
       FORMAT 14: Motorola Exormax S2 Code 87
       Supports S1 (4-char address) and S2 (6-char address) records.
       For data that fits in 16-bit address space, S1 records are used;
       otherwise S2 records with 24-bit addresses.
       S1 record: S1 BC AAAA data SS  (BC = recLen+3, checksum over BC+addr+data)
       S2 record: S2 BC AAAAAA data SS (BC = recLen+4, checksum over BC+addr+data)
       S1 end  : S9 03 0000 FC
       S2 end  : S8 04 000000 FB
       ================================================================== */
    else if (fmt == 14) {
        int useS2 = (byteCount > 0xFFFFUL) ? 1 : 0;

        /* Optional S0 header */
        {
            const char *hdr = "HDR";
            unsigned int hcs = 0;
            unsigned int hlen = (unsigned int)(strlen(hdr) + 3);
            hcs = hlen + 0x00 + 0x00;
            for (j = 0; j < (unsigned int)strlen(hdr); j++)
                hcs += (unsigned char)hdr[j];
            hcs = (~hcs) & 0xFF;
            slen = sprintf(srbuf, "S0%02X0000%s%02X\r\n", hlen, hdr, hcs);
            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            delay(20);
        }

        laddr = 0;
        while (laddr < byteCount) {
            unsigned int bc;
            if (CheckAbort(laddr, byteCount)) return;
            recLen = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);

            if (!useS2) {
                /* S1: BC = recLen + 3  (2 addr bytes + 1 checksum) */
                bc   = recLen + 3;
                sc   = bc;
                sc  += (unsigned int)((laddr >> 8) & 0xFF);
                sc  += (unsigned int)( laddr        & 0xFF);
                slen = sprintf(srbuf, "S1%02X%04lX", bc, laddr);
            } else {
                /* S2: BC = recLen + 4  (3 addr bytes + 1 checksum) */
                bc   = recLen + 4;
                sc   = bc;
                sc  += (unsigned int)((laddr >> 16) & 0xFF);
                sc  += (unsigned int)((laddr >>  8) & 0xFF);
                sc  += (unsigned int)( laddr         & 0xFF);
                slen = sprintf(srbuf, "S2%02X%06lX", bc, laddr);
            }

            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                sc  += byte;
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            sc   = (~sc) & 0xFF;
            slen += sprintf(srbuf + slen, "%02X\r\n", sc);

            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            laddr += recLen;
            delay(5);
        }

        /* end record */
        if (!useS2) {
            SendStr("S9030000FC\r\n");
        } else {
            SendStr("S804000000FB\r\n");
        }
    }

    /* ==================================================================
       FORMAT 15: Intel MCS-86 HEX Code 88
       Same structure as Code 83 (Intel HEX) for data and end records.
       For data beyond 64K a type-02 extended-address record is prepended;
       within 64K only type-00 data records and the type-01 end record
       are needed.  Structure is identical to standard Intel HEX.
       ================================================================== */
    else if (fmt == 15) {
        unsigned int  seg;              /* last emitted segment value */
        unsigned int  curSeg;
        unsigned int  esa;              /* extended segment address */
        unsigned int  esc;              /* extended segment checksum */
        unsigned int  offset;           /* 16-bit offset within current segment */
        seg = 0xFFFFU;                  /* force extended-address record on first pass */

        laddr = 0;
        while (laddr < byteCount) {
            if (CheckAbort(laddr, byteCount)) return;
            recLen = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);

            /* emit type-02 extended segment address record when segment changes */
            curSeg = (unsigned int)((laddr >> 4) & 0xF000U);
            if (curSeg != seg) {
                seg = curSeg;
                /* ESA = (laddr >> 4) & 0xFFFF; linear = (ESA << 4) + 16-bit offset */
                esa  = (unsigned int)((laddr >> 4) & 0xFFFFU);
                esc  = 0x02 + 0x00 + 0x00 + 0x02
                     + (unsigned int)((esa >> 8) & 0xFF)
                     + (unsigned int)( esa        & 0xFF);
                esc  = (~esc + 1) & 0xFF;
                slen = sprintf(srbuf, ":02000002%04X%02X\r\n", esa, esc);
                for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                    WriteCOM1((unsigned char)srbuf[j]);
                delay(5);
            }

            /* data record - identical structure to Code 83 */
            offset = (unsigned int)(laddr & 0xFFFFUL);
            sc  = recLen;
            sc += (unsigned int)((offset >> 8) & 0xFF);
            sc += (unsigned int)( offset        & 0xFF);
            sc += 0x00;

            slen = sprintf(srbuf, ":%02X%04X00",
                           (unsigned int)recLen, offset);
            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                sc  += byte;
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            sc   = (~sc + 1) & 0xFF;
            slen += sprintf(srbuf + slen, "%02X\r\n", sc);

            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            laddr += recLen;
            delay(5);
        }

        SendStr(":00000001FF\r\n");
    }

    /* ==================================================================
       FORMAT 16: Motorola 32Bit S3 Code 95
       S3 record: S3 BC AAAAAAAA data SS
         BC = recLen + 5  (4 address bytes + 1 checksum)
         AAAAAAAA = 8-hex-char (32-bit) address
         Checksum = one's complement of (BC + all_addr_bytes + all_data_bytes) & 0xFF
       S7 end record: S7 05 00000000 FA
       ================================================================== */
    else if (fmt == 16) {
        /* Optional S0 header */
        {
            const char *hdr = "HDR";
            unsigned int hcs = 0;
            unsigned int hlen = (unsigned int)(strlen(hdr) + 3);
            hcs = hlen + 0x00 + 0x00;
            for (j = 0; j < (unsigned int)strlen(hdr); j++)
                hcs += (unsigned char)hdr[j];
            hcs = (~hcs) & 0xFF;
            slen = sprintf(srbuf, "S0%02X0000%s%02X\r\n", hlen, hdr, hcs);
            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            delay(20);
        }

        laddr = 0;
        while (laddr < byteCount) {
            unsigned int bc_s3;         /* byte count for S3 record */
            if (CheckAbort(laddr, byteCount)) return;
            recLen  = (byteCount - laddr >= 16UL) ? 16 : (unsigned int)(byteCount - laddr);
            bc_s3   = recLen + 5;       /* 4-byte addr + 1 checksum byte */
            sc      = bc_s3;
            sc     += (unsigned int)((laddr >> 24) & 0xFF);
            sc     += (unsigned int)((laddr >> 16) & 0xFF);
            sc     += (unsigned int)((laddr >>  8) & 0xFF);
            sc     += (unsigned int)( laddr         & 0xFF);

            slen = sprintf(srbuf, "S3%02X%08lX",
                           (unsigned int)bc_s3, laddr);
            for (j = 0; j < recLen; j++) {
                byte = DataBuffer[laddr + j];
                sc  += byte;
                slen += sprintf(srbuf + slen, "%02X", (unsigned int)byte);
            }
            sc   = (~sc) & 0xFF;
            slen += sprintf(srbuf + slen, "%02X\r\n", sc);

            for (j = 0; (unsigned int)j < (unsigned int)slen; j++)
                WriteCOM1((unsigned char)srbuf[j]);
            laddr += recLen;
            delay(5);
        }

        /* S7 end record: byte count=05, address=00000000, checksum=FA */
        SendStr("S70500000000FA\r\n");
    }

    /* ---- completion message ---- */
    {
        char msg[50];
        sprintf(msg, "Sent %lu bytes successfully.", byteCount);
        DlgMessage(" Upload Complete ", msg, "", 0);
    }
}
#endif /* PP_IN(3) */

#if PP_IN(4)
void LoadFileIntoBuffer(void) {
    char filename[MAX_PATH];
    FILE *fp;
    long fileSize;
    int i, r, w, h, barX, filled, ch;
    unsigned char nearBuf[512];
    int bytesRead;
    unsigned int j, chunk;
    unsigned long total;
    char line[64];

    RedrawScreenFn();

    {
        filename[0] = '\0';
        if (!DlgFileBrowser(" Open File ", "*.*", filename, 48)) {
            BufferSize = 0; DisplayHexEditor(); return;
        }
    }
    if (filename[0] == '\0') { BufferSize = 0; DisplayHexEditor(); return; }

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        DlgMessage(" Error ", "Cannot open file:", filename, 1);
        BufferSize = 0; return;
    }

    fseek(fp, 0L, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    if (fileSize < 0) fileSize = 0;
    total = (unsigned long)fileSize;

    RedrawScreenFn();

    w = 60; h = 10;
    r = DlgDrawAt((SCREEN_W - w) / 2 + 1, 4, w, h, " Loading File ");
    sprintf(line, "File: %.40s", filename);
    DlgLineLeft(r,     w, line);
    sprintf(line, "Size: %lu bytes", total);
    DlgLineLeft(r + 1, w, line);

    barX = (SCREEN_W - w) / 2 + 2;

    BufferSize = 0;
    while (BufferSize < MAX_BUFFER_SIZE) {
        chunk = (MAX_BUFFER_SIZE - BufferSize > 512UL)
                ? 512 : (unsigned int)(MAX_BUFFER_SIZE - BufferSize);
        bytesRead = fread(nearBuf, 1, chunk, fp);
        if (bytesRead <= 0) break;
        for (j = 0; j < (unsigned int)bytesRead; j++)
            DataBuffer[BufferSize + j] = nearBuf[j];
        BufferSize += (unsigned int)bytesRead;

        gotoxy(barX, r + 2);
        textcolor(CLR_DLG_OK);
        textbackground(CLR_WIN_BG);
        filled = (total > 0)
                 ? (int)(((unsigned long)BufferSize * (unsigned long)UPLOAD_BAR_WIDTH) / total)
                 : UPLOAD_BAR_WIDTH;
        if (filled > UPLOAD_BAR_WIDTH) filled = UPLOAD_BAR_WIDTH;
        for (i = 0; i < filled; i++) cprintf("\xDB");
        textcolor(CLR_DLG_BORDER);
        for (; i < UPLOAD_BAR_WIDTH; i++) cprintf("\xB0");

        sprintf(line, "%lu / %lu bytes", BufferSize, total);
        gotoxy(barX, r + 3);
        textcolor(CLR_WIN_FG);
        textbackground(CLR_WIN_BG);
        cprintf("%-50s", line);
    }
    fclose(fp);

    strncpy(CurrentFileName, filename, 79);
    CurrentFileName[79] = '\0';

    sprintf(line, "Loaded %lu bytes successfully.", BufferSize);
    DlgLineLeft(r + 4, w, line);

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
    gotoxy((SCREEN_W - 28) / 2 + 1, r + 6);
    cprintf("[ Press ENTER to continue ]");
    do { ch = getch(); } while (ch != 13);
}

void DrawSmallBox(int x, int y, int w, int h, const char *label) {
    int i, j, tlen, labelX;

    textcolor(WHITE);
    textbackground(BLACK);
    gotoxy(x, y);
    cprintf("\xDA");
    for (i = 0; i < w - 2; i++) cprintf("\xC4");
    cprintf("\xBF");

    for (i = 1; i < h - 1; i++) {
        gotoxy(x, y + i);
        cprintf("\xB3");
        for (j = 0; j < w - 2; j++) cprintf(" ");
        cprintf("\xB3");
    }

    gotoxy(x, y + h - 1);
    cprintf("\xC0");
    for (i = 0; i < w - 2; i++) cprintf("\xC4");
    cprintf("\xD9");

    tlen = strlen(label);
    labelX = x + (w - tlen) / 2;
    gotoxy(labelX, y + h / 2);
    cprintf("%s", label);
}

void DrawConnectionPanel(int topRow) {
    int panelW = 56, panelStartX = (SCREEN_W - panelW) / 2 + 1;
    int boxW = 18;
    int leftX = panelStartX + 2;
    int rightX = panelStartX + panelW - 2 - boxW;
    int arrowX = leftX + boxW;
    int arrowW = rightX - arrowX;
    int r, c;

    textcolor(CLR_WIN_FG);
    textbackground(CLR_MENUBAR_BG);
    for (r = topRow; r < topRow + 7; r++) {
        gotoxy(panelStartX, r);
        for (c = 0; c < panelW; c++) cprintf(" ");
    }

    DrawSmallBox(leftX, topRow + 1, boxW, 5, "PC");
    DrawSmallBox(rightX, topRow + 1, boxW, 5, "PROGRAMMER");

    textcolor(BLACK);
    textbackground(CLR_MENUBAR_BG);
    gotoxy(arrowX, topRow + 3);
    cprintf("\x11");
    for (c = 0; c < arrowW - 2; c++) cprintf("\xC4");
    cprintf("\x10");

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

void DrawStatusField(int row, int boxStartX, const char *label, const char *value) {
    gotoxy(boxStartX + 2, row);
    textcolor(CLR_WIN_HDR);
    textbackground(CLR_WIN_BG);
    cprintf("%12s ", label);
    textcolor(CLR_DLG_BORDER);
    cprintf("\xB3");
    textcolor(CLR_WIN_FG);
    cprintf(" %s", value);
}

void DrawSystemStatusBox(void) {
    int startX = (SCREEN_W - 60) / 2 + 1, startY = 4, w = 60, r;
    char fillStr[4];
    char deviceStr[96];
    char lbl[40];

    r = DlgDrawAt(startX, startY, w, 10, " System Status ");
    ComPortLabel(lbl, (int)sizeof(lbl));

    sprintf(fillStr, "%02X", FillByte);
    if (ActiveProfile >= 0)
        sprintf(deviceStr, "%.24s%s %.12s, %lu baud", lbl, ComPortPresent() ? "" : " (NOT FOUND)", Profiles[ActiveProfile].name, Profiles[ActiveProfile].baud);
    else
        sprintf(deviceStr, "%.24s%s, 9600 baud (default)", lbl, ComPortPresent() ? "" : " (NOT FOUND)");

    DrawStatusField(r,     startX, "Task",        "Awaiting Selection");
    DrawStatusField(r + 1, startX, "Data Source", "None");
    DrawStatusField(r + 2, startX, "Sumcheck",    "?");
    DrawStatusField(r + 3, startX, "Device",      deviceStr);
    DrawStatusField(r + 4, startX, "Process",     "Ready");
    DrawStatusField(r + 5, startX, "Notes",       "Fill Byte:");
    gotoxy(startX + 16, r + 6);
    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
    cprintf("%s (change via Options menu)", fillStr);

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
}

void DisplayStartupScreen(void) {
    int i;

    _setcursortype(_NOCURSOR);
    clrscr();

    gotoxy(1, 1);
    textcolor(CLR_BANNER_FG);
    textbackground(CLR_BANNER_BG);
    cprintf(" EPROM Editor v1.6");
    gotoxy(ConW - 19, 1);
    cprintf("Mickey W. Lawless");
    cprintf("%2s", "");

    ComputeMenuBarCols();
    DrawTopMenuBar(-1);

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
    for (i = 3; i < ConH; i++) { gotoxy(1, i); cprintf("%-*s", ConW, ""); }

    DrawSystemStatusBox();
    DrawConnectionPanel(15);

    gotoxy(1, ConH);
    textcolor(CLR_MENUBAR_FG);
    textbackground(CLR_MENUBAR_BG);
    cprintf("%-*s", ConW - 1, "");
    gotoxy(2, ConH);
    cprintf("Select menu with Alt+highlighted letter");
    gotoxy(ConW - 22, ConH);
    cprintf("< F1=Help  F11=Size >");

    textcolor(CLR_WIN_FG);
    textbackground(CLR_WIN_BG);
    _setcursortype(_NORMALCURSOR);
}

/* OpenCategoryStartup - dropdown dispatch for the pre-buffer startup
   screen. Returns the same 1-4 menuChoice codes main() has always used
   (1=Read COM1, 2=Load file, 3=Empty editor, 4=Exit), or 0 for "stay
   on the startup screen" (nothing chosen, or an info box was shown). */
/* OpenCategoryStartup - dropdown dispatch for the pre-buffer startup
   screen. Returns the same 1-4 menuChoice codes main() has always used
   (1=Read COM1, 2=Load file, 3=Empty editor, 4=Exit), or 0 for "stay
   on the startup screen" (nothing chosen, or an info box was shown).
   PulldownList returns a negative value when the user pressed a
   DIFFERENT category's Alt-key while this dropdown was open - in
   that case we switch straight to that category instead of stopping. */
int OpenCategoryStartup(int cat) {
    int r, nextCat, choice;
    while (cat >= 0) {
        nextCat = -1;
        choice = 0;
        switch (cat) {
            case MB_SETUP: {
                static const char *items[] = {
                    ("Read from " PP_PORTNAME), "Select Programmer", "New Programmer",
                    "Edit Programmer", "Delete Programmer"
                };
                r = PulldownList(MenuBarCol[MB_SETUP], 3, " Setup ", items, 5);
                if (r < 0) nextCat = -r - 1;
                else if (r == 1) choice = 1;
                else if (r == 2) SelectProgrammer();
                else if (r == 3) NewProgrammer();
                else if (r == 4) EditProgrammer();
                else if (r == 5) DeleteProgrammer();
                break;
            }
            case MB_DATA: {
                static const char *items[] = { "Load File into Buffer", "Enter Empty Editor" };
                r = PulldownList(MenuBarCol[MB_DATA], 3, " Data ", items, 2);
                if (r < 0) nextCat = -r - 1;
                else if (r == 1) choice = 2;
                else if (r == 2) choice = 3;
                break;
            }
            case MB_PROGRAM:
            case MB_TASKS:
            case MB_UTILITIES: {
                static const char *msgTitle[3] = { " Program ", " Tasks ", " Utilities " };
                DlgMessage(msgTitle[cat - MB_PROGRAM], "Load data first", "(use the Data menu)", 0);
                break;
            }
            case MB_OPTIONS: {
                static const char *items[] = { "Fill Byte Value", "Byte Grouping", PP_PORTNAME, "Window Size" };
                r = PulldownList(MenuBarCol[MB_OPTIONS], 3, " Options ", items, 4);
                if (r < 0) nextCat = -r - 1;
                else if (r == 1) SetFillByte();
                else if (r == 2) SetGroupSize();
                else if (r == 3) SetComPort();
                else if (r == 4) SetWindowSize();
                break;
            }
            case MB_EXIT:
                choice = 4;
                break;
            case MB_HELP:
                DlgMessage(" Help ", "EPROM Data Reader & Hex Editor v1.6",
                           "Alt+letter opens a menu. F11 = maximize.", 0);
                break;
        }
        if (choice) return choice;
        if (nextCat >= 0) DisplayStartupScreen();
        cat = nextCat;
    }
    return 0;
}

int ShowStartupMenu(void) {
    int ch, extch, i, choice;

    textmode(C80);
    RedrawScreenFn = DisplayStartupScreen;
    DisplayStartupScreen();

    while (1) {
        ch = getch();
        if (ch == 0 || ch == 224) {
            extch = getch();
            if (extch == SCAN_RESIZE) { DisplayStartupScreen(); continue; }
            if (extch == SCAN_F1) {
                choice = OpenCategoryStartup(MB_HELP);
                if (choice) { RedrawScreenFn = DisplayHexEditor; return choice; }
                DisplayStartupScreen();
                continue;
            }
            for (i = 0; i < MENUBAR_COUNT; i++) {
                if (extch == MenuBarAltScan[i]) {
                    choice = OpenCategoryStartup(i);
                    if (choice) { RedrawScreenFn = DisplayHexEditor; return choice; }
                    DisplayStartupScreen();
                    break;
                }
            }
        } else if (ch >= '1' && ch <= '4') {
            RedrawScreenFn = DisplayHexEditor;
            return ch - '0';
        } else if (ch == 27) {
            RedrawScreenFn = DisplayHexEditor;
            return 4;
        }
    }
}

/* -----------------------------------------------------------------------
   New Tasks/Utilities/Options features - replace the removed disassembler
   ----------------------------------------------------------------------- */

/* SetWindowSize - Options > Window Size. Presets, or maximize/restore
   (same as F11). Works in the classic console host; in Windows Terminal
   the window can simply be dragged and the UI follows. */
void SetWindowSize(void) {
    static const char *items[] = {
        " 80 x 25   (standard)", "100 x 30", "120 x 40", "132 x 50", "160 x 60",
        "Maximize / Restore  (F11)"
    };
    static const int sw[5] = { 80, 100, 120, 132, 160 };
    static const int sh[5] = { 25, 30, 40, 50, 60 };
    int r;

    r = PulldownList(MenuBarCol[MB_OPTIONS], 3, " Window Size ", items, 6);
    if (r >= 1 && r <= 5) {
        ConSetWindow(sw[r - 1], sh[r - 1]);
        conMaxed = 0;
        ConSyncSize();
    } else if (r == 6) {
        ConToggleMax();
    }
    RedrawScreenFn();
}

void SetFillByte(void) {
    char input[6];
    unsigned int val;
    int w = 50, h = 8, r;

    sprintf(input, "%02X", FillByte);
    r = DlgDraw(w, h, " Fill Byte Value ");
    DlgLine(r,     w, "Enter hex byte value (00-FF):");
    DlgLine(r + 1, w, "Used by Utilities > Fill Range.");
    if (DlgInput(r + 3, w, "Value:", input, 2) && input[0] != '\0') {
        val = 0;
        sscanf(input, "%X", &val);
        FillByte = (unsigned char)(val & 0xFF);
    } else {
        RedrawScreenFn();
    }
}

void SetGroupSize(void) {
    static const char *items[] = { "1 byte", "2 bytes", "4 bytes", "8 bytes", "16 bytes (ungrouped)" };
    static const int sizes[5] = { 1, 2, 4, 8, 16 };
    int r;
    r = DlgMenu(" Byte Grouping ", items, 5);
    if (r) GroupSize = sizes[r - 1];
}

#endif /* PP_IN(4) (resumes after the platform helpers) */

#if PP_POSIX
/* ========================================================================
   POSIX platform helpers: config files, serial-port discovery, directory
   listing for the file browser, printing.
   ======================================================================== */
#define COMCFG_FILENAME "PROMCOM.CFG"
char ProfilePath[MAX_PATH + 32];
char ComCfgPath[MAX_PATH + 32];

/* Config files live in ~/.prompro/ */
void InitConfigPaths(const char *argv0) {
    char dir[MAX_PATH];
    const char *home = getenv("HOME");
    (void)argv0;
    if (home == NULL || home[0] == '\0') home = ".";
    snprintf(dir, sizeof(dir), "%s/.prompro", home);
    mkdir(dir, 0755);
    snprintf(ProfilePath, sizeof(ProfilePath), "%s/%s", dir, CONFIG_FILENAME);
    snprintf(ComCfgPath, sizeof(ComCfgPath), "%s/%s", dir, COMCFG_FILENAME);
}

void SaveComPort(void) {
    FILE *fp = fopen(ComCfgPath, "w");
    if (fp == NULL) return;
    fprintf(fp, "%s\n", ComDevPath);
    fclose(fp);
}

void LoadComPort(void) {
    FILE *fp = fopen(ComCfgPath, "r");
    char line[sizeof(ComDevPath)];
    size_t n;
    if (fp == NULL) return;
    if (fgets(line, sizeof(line), fp) != NULL) {
        n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n > 0) strcpy(ComDevPath, line);
    }
    fclose(fp);
}

/* Serial ports the system currently has. macOS: /dev/cu.* (the call-out
   side - tty.* blocks waiting for carrier). Linux: ttyUSB*, ttyACM*, and
   the ttyS* that have a UART behind them. BSD: cuaU*, cuaa*. */
#define MAX_DETECTED 24
char DetectedDev[MAX_DETECTED][80];
int  DetectedCount = 0;

static int PortNameOk(const char *n) {
#if defined(__APPLE__)
    if (strncmp(n, "cu.", 3) != 0) return 0;
    if (strstr(n, "Bluetooth") || strstr(n, "debug-console") ||
        strstr(n, "BLTH") || strstr(n, "MALS") || strstr(n, "SOC")) return 0;
    return 1;
#elif defined(__linux__)
    return strncmp(n, "ttyUSB", 6) == 0 || strncmp(n, "ttyACM", 6) == 0 ||
           strncmp(n, "ttyS", 4) == 0 || strncmp(n, "rfcomm", 6) == 0;
#else
    return strncmp(n, "cua", 3) == 0 && strstr(n, ".") == NULL;
#endif
}

#ifdef __linux__
static int LinuxUartPresent(const char *path) {
    struct serial_struct ss;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    int ok = 0;
    if (fd < 0) return 0;
    if (ioctl(fd, TIOCGSERIAL, &ss) == 0) ok = (ss.type != PORT_UNKNOWN);
    else ok = 1;                       /* USB/ACM/rfcomm have no serial_struct */
    close(fd);
    return ok;
}
#endif

void DetectComPorts(void) {
    DIR *d;
    struct dirent *de;
    int i, j;
    char tmp[80];

    DetectedCount = 0;
    d = opendir("/dev");
    if (d == NULL) return;
    while ((de = readdir(d)) != NULL && DetectedCount < MAX_DETECTED) {
        char path[sizeof(DetectedDev[0])];
        if (!PortNameOk(de->d_name)) continue;
        if (strlen(de->d_name) + 6 > sizeof(path)) continue;
        snprintf(path, sizeof(path), "/dev/%s", de->d_name);
#ifdef __linux__
        if (strncmp(de->d_name, "ttyS", 4) == 0 && !LinuxUartPresent(path)) continue;
#endif
        strcpy(DetectedDev[DetectedCount++], path);
    }
    closedir(d);
    for (i = 1; i < DetectedCount; i++) {       /* sort by name */
        strcpy(tmp, DetectedDev[i]);
        j = i - 1;
        while (j >= 0 && strcmp(DetectedDev[j], tmp) > 0) {
            strcpy(DetectedDev[j + 1], DetectedDev[j]);
            j--;
        }
        strcpy(DetectedDev[j + 1], tmp);
    }
}

void ComPortLabel(char *out, int n) {
    const char *b;
    if (ComDevPath[0] == '\0') { strncpy(out, "(no port)", (size_t)n - 1); out[n - 1] = '\0'; return; }
    b = ComDevPath;
    if (strncmp(b, "/dev/", 5) == 0) b += 5;       /* cu.usbserial-1410, pts/3, ... */
    strncpy(out, b, (size_t)n - 1);
    out[n - 1] = '\0';
}

int ComPortPresent(void) {
    return ComDevPath[0] != '\0' && access(ComDevPath, R_OK | W_OK) == 0;
}

void DescribeComPorts(char *out, int outLen) {
    int i;
    DetectComPorts();
    if (DetectedCount == 0) {
        strncpy(out, "No serial ports found - plug in adapter.", (size_t)outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    strcpy(out, "Found:");
    for (i = 0; i < DetectedCount; i++) {
        const char *b = strrchr(DetectedDev[i], '/');
        b = b ? b + 1 : DetectedDev[i];
        if ((int)(strlen(out) + strlen(b) + 2) >= outLen) break;
        strcat(out, " ");
        strcat(out, b);
    }
}

/* Command line: "-p DEVICE" or a bare /dev/... argument picks the port. */
int Plat_ParseArg(int argc, char **argv, int *ai) {
    const char *a = argv[*ai];
    if (strcmp(a, "-p") == 0 && *ai + 1 < argc) {
        (*ai)++;
        strncpy(ComDevPath, argv[*ai], sizeof(ComDevPath) - 1);
        ComDevPath[sizeof(ComDevPath) - 1] = '\0';
        return 1;
    }
    if (strncmp(a, "/dev/", 5) == 0) {
        strncpy(ComDevPath, a, sizeof(ComDevPath) - 1);
        ComDevPath[sizeof(ComDevPath) - 1] = '\0';
        return 1;
    }
    return 0;
}

/* No port given and the saved one is gone: take the first one found. */
void Plat_AutoSelectPort(int given) {
    DetectComPorts();
    if (!given && !ComPortPresent() && DetectedCount > 0) {
        strncpy(ComDevPath, DetectedDev[0], sizeof(ComDevPath) - 1);
        ComDevPath[sizeof(ComDevPath) - 1] = '\0';
    }
}

void SetComPort(void) {
    static char labels[MAX_DETECTED + 1][80];
    static const char *items[MAX_DETECTED + 1];
    char input[sizeof(ComDevPath)];
    int w = 60, h = 8, r, i, n, pick;

    DetectComPorts();
    n = 0;
    for (i = 0; i < DetectedCount; i++) {
        sprintf(labels[n], "%.50s%s", DetectedDev[i],
                strcmp(DetectedDev[i], ComDevPath) == 0 ? "  (current)" : "");
        items[n] = labels[n];
        n++;
    }
    strcpy(labels[n], "Other - type a device path...");
    items[n] = labels[n];
    n++;

    pick = DlgMenu(" Select Serial Port ", items, n);
    if (pick == 0) return;
    if (pick <= DetectedCount) {
        strcpy(ComDevPath, DetectedDev[pick - 1]);
        SaveComPort();
        return;
    }

    strcpy(input, ComDevPath);
    r = DlgDraw(w, h, " Serial Port ");
    DlgLine(r,     w, "Enter the device path, e.g. /dev/cu.usbserial-1410");
    DlgLine(r + 1, w, "Used by Read / Upload (also -p DEVICE).");
    if (DlgInput(r + 3, w, "Port:", input, 48) && input[0] != '\0') {
        strcpy(ComDevPath, input);
        SaveComPort();
    }
    RedrawScreenFn();
}

/* ---- file browser listing --------------------------------------------- */
static int WildMatch(const char *pat, const char *s) {
    if (strcmp(pat, "*.*") == 0) pat = "*";
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            for (; *s; s++) if (WildMatch(pat, s)) return 1;
            return WildMatch(pat, s);
        }
        if (*s == '\0') return 0;
        if (*pat != '?' && toupper((unsigned char)*pat) != toupper((unsigned char)*s)) return 0;
        pat++; s++;
    }
    return *s == '\0';
}

/* Appends entries to fbNames/fbSizes/fbIsDir from index `start`; returns
   how many were added. wantDirs: subdirectories ("filter" ignored), else
   regular files matching `filter`. Hidden (dot) entries are skipped. */
int Plat_FbEnum(int wantDirs, const char *filter, int start) {
    DIR *d = opendir(".");
    struct dirent *de;
    struct stat st;
    int n = start;
    if (d == NULL) return 0;
    while ((de = readdir(d)) != NULL && n < FB_MAX_FILES - 1) {
        if (de->d_name[0] == '.') continue;
        if (stat(de->d_name, &st) != 0) continue;
        if (wantDirs) {
            if (!S_ISDIR(st.st_mode)) continue;
        } else {
            if (!S_ISREG(st.st_mode) || !WildMatch(filter, de->d_name)) continue;
        }
        strncpy(fbNames[n], de->d_name, FB_NAMELEN - 1);
        fbNames[n][FB_NAMELEN - 1] = '\0';
        fbSizes[n] = wantDirs ? 0UL : (unsigned long)st.st_size;
        fbIsDir[n] = (char)(wantDirs ? 1 : 0);
        n++;
    }
    closedir(d);
    return n - start;
}

void Plat_GetCwd(char *buf, int n) {
    if (getcwd(buf, (size_t)n) == NULL) { buf[0] = '/'; buf[1] = '\0'; }
}
void Plat_Chdir(const char *p) { if (chdir(p) != 0) { } }
int  Plat_AtRoot(const char *cwd) { return strcmp(cwd, "/") == 0; }

/* ---- printer: pipe the listing to lpr --------------------------------- */
FILE *Plat_PrintOpen(void) { return popen("lpr", "w"); }
void  Plat_PrintClose(FILE *fp) { pclose(fp); }
#else
/* ========================================================================
   Platform helpers for COM-numbered systems (Windows, MS-DOS): config
   files, COM port discovery, directory listing for the file browser,
   printing.
   ======================================================================== */
/* Programmer profiles: a raw binary dump of the profile array. Simple on
   purpose: this file is private to this program, read and written by the
   exact same struct layout, so there's no need for a portable/versioned
   format. Config files live next to the EXE (not the current directory,
   which the file browser changes). */
#define COMCFG_FILENAME "PROMCOM.CFG"
#if PP_IN(1)
char ProfilePath[MAX_PATH + 16];
char ComCfgPath[MAX_PATH + 16];

void InitConfigPaths(const char *argv0) {
    char *slash;
#if PP_WIN32
    DWORD n;
    (void)argv0;
    n = GetModuleFileNameA(NULL, ProfilePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        strcpy(ProfilePath, CONFIG_FILENAME);
        strcpy(ComCfgPath, COMCFG_FILENAME);
        return;
    }
    slash = strrchr(ProfilePath, '\\');
#else
    /* DOS 3.0+ passes the full path of the program as argv[0]. */
    if (argv0 == NULL || argv0[0] == '\0' || strlen(argv0) >= MAX_PATH) {
        strcpy(ProfilePath, CONFIG_FILENAME);
        strcpy(ComCfgPath, COMCFG_FILENAME);
        return;
    }
    strcpy(ProfilePath, argv0);
    slash = strrchr(ProfilePath, '\\');
    if (slash == NULL) slash = strrchr(ProfilePath, ':');
#endif
    if (slash) *(slash + 1) = '\0'; else ProfilePath[0] = '\0';
    strcpy(ComCfgPath, ProfilePath);
    strcat(ProfilePath, CONFIG_FILENAME);
    strcat(ComCfgPath, COMCFG_FILENAME);
}

void SaveComPort(void) {
    FILE *fp = fopen(ComCfgPath, "w");
    if (fp == NULL) return;
    fprintf(fp, "%d\n", ComPortNum);
    fclose(fp);
}

void LoadComPort(void) {
    FILE *fp = fopen(ComCfgPath, "r");
    int v;
    if (fp == NULL) return;
    if (fscanf(fp, "%d", &v) == 1 && v >= 1 && v <= PP_MAXCOM) ComPortNum = v;
    fclose(fp);
}

#define MAX_DETECTED 12
int DetectedPorts[MAX_DETECTED];
int DetectedCount = 0;

#if PP_WIN32
/* COM ports Windows currently knows about - a USB serial adapter normally
   shows up as COM3 or higher, not COM1. */
static void AddDetectedPort(const char *s) {
    const char *q;
    int num;
    if (_strnicmp(s, "COM", 3) != 0 || s[3] < '1' || s[3] > '9') return;
    for (q = s + 3; *q >= '0' && *q <= '9'; q++) ;
    if (*q) return;
    num = atoi(s + 3);
    if (num <= 255 && DetectedCount < MAX_DETECTED)
        DetectedPorts[DetectedCount++] = num;
}

/* Prefers HKLM\HARDWARE\DEVICEMAP\SERIALCOMM, which lists only serial
   ports that are actually present (QueryDosDevice also reports a legacy
   COM1 name with no device behind it - that opens with error 433). Falls
   back to QueryDosDevice if the key can't be read. */
void DetectComPorts(void) {
    static char buf[65536];
    char *p;
    int i, j, t;
    HKEY hk;
    DWORD idx, nameLen, dataLen, type;
    char name[64], data[64];

    DetectedCount = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        for (idx = 0; ; idx++) {
            nameLen = sizeof(name);
            dataLen = sizeof(data) - 1;
            if (RegEnumValueA(hk, idx, name, &nameLen, NULL, &type, (LPBYTE)data, &dataLen) != ERROR_SUCCESS) break;
            if (type != REG_SZ) continue;
            data[sizeof(data) - 1] = '\0';
            AddDetectedPort(data);
        }
        RegCloseKey(hk);
    } else if (QueryDosDeviceA(NULL, buf, sizeof(buf)) != 0) {
        for (p = buf; *p; p += strlen(p) + 1) AddDetectedPort(p);
    }
    for (i = 1; i < DetectedCount; i++) {
        t = DetectedPorts[i];
        j = i - 1;
        while (j >= 0 && DetectedPorts[j] > t) { DetectedPorts[j + 1] = DetectedPorts[j]; j--; }
        DetectedPorts[j + 1] = t;
    }
}
#else /* DOS */
/* COM1..COM4 that the BIOS found at start-up (0040:0000 table). */
void DetectComPorts(void) {
    int n;
    DetectedCount = 0;
    for (n = 1; n <= 4; n++)
        if (ComBaseFor(n) != 0 && *(unsigned int far *)MK_FP(0x40, (n - 1) * 2) != 0)
            DetectedPorts[DetectedCount++] = n;
}
#endif

int ComPortExists(int n) {
    int i;
    DetectComPorts();
    for (i = 0; i < DetectedCount; i++)
        if (DetectedPorts[i] == n) return 1;
    return 0;
}

void ComPortLabel(char *out, int n) {
    char tmp[16];
    sprintf(tmp, "COM%d", ComPortNum);
    strncpy(out, tmp, (size_t)n - 1);
    out[n - 1] = '\0';
}

int ComPortPresent(void) { return ComPortExists(ComPortNum); }

void DescribeComPorts(char *out, int outLen) {
    char tmp[16];
    int i;
    DetectComPorts();
    if (DetectedCount == 0) { strncpy(out, "No active COM ports found.", outLen - 1); out[outLen - 1] = '\0'; return; }
    strcpy(out, "Detected:");
    for (i = 0; i < DetectedCount; i++) {
        sprintf(tmp, " COM%d", DetectedPorts[i]);
        if ((int)(strlen(out) + strlen(tmp)) >= outLen) break;
        strcat(out, tmp);
    }
}

/* "/COMn" on the command line picks the port. */
int Plat_ParseArg(int argc, char **argv, int *ai) {
    const char *a = argv[*ai];
    int n;
    (void)argc;
    if ((a[0] == '/' || a[0] == '-') && PP_STRNICMP(a + 1, "COM", 3) == 0) {
        n = atoi(a + 4);
        if (n >= 1 && n <= PP_MAXCOM) { ComPortNum = n; return 1; }
    }
    return 0;
}

/* No explicit port and the saved/default one isn't present: use the first
   COM port the system actually reports (USB adapters are rarely COM1). */
void Plat_AutoSelectPort(int given) {
    if (!given && !ComPortExists(ComPortNum) && DetectedCount > 0)
        ComPortNum = DetectedPorts[0];
}

void SetComPort(void) {
    static char labels[MAX_DETECTED + 1][24];
    static const char *items[MAX_DETECTED + 1];
    char input[6];
    int w = 50, h = 8, r, v, i, n, pick;

    DetectComPorts();
    n = 0;
    for (i = 0; i < DetectedCount; i++) {
        sprintf(labels[n], "COM%d%s", DetectedPorts[i], (DetectedPorts[i] == ComPortNum) ? "  (current)" : "");
        items[n] = labels[n];
        n++;
    }
    strcpy(labels[n], "Other - type a number...");
    items[n] = labels[n];
    n++;

    pick = DlgMenu(" Select COM Port ", items, n);
    if (pick == 0) return;
    if (pick <= DetectedCount) {
        ComPortNum = DetectedPorts[pick - 1];
        SaveComPort();
        return;
    }

    sprintf(input, "%d", ComPortNum);
    r = DlgDraw(w, h, " COM Port ");
    DlgLine(r,     w, "Enter COM port number:");
    DlgLine(r + 1, w, "Used by Read / Upload (also /COMn).");
    if (DlgInput(r + 3, w, "Port:", input, 3) && input[0] != '\0') {
        v = atoi(input);
        if (v >= 1 && v <= PP_MAXCOM) {
            ComPortNum = v;
            SaveComPort();
        }
    }
    RedrawScreenFn();
}

/* ---- file browser listing --------------------------------------------- */
#if PP_WIN32
int Plat_FbEnum(int wantDirs, const char *filter, int start) {
    WIN32_FIND_DATAA fd;
    HANDLE hFind;
    int n = start;
    hFind = FindFirstFileA(wantDirs ? "*" : filter, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if (n >= FB_MAX_FILES - 1) break;
        if (wantDirs) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ||
                strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        } else {
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        }
        strncpy(fbNames[n], fd.cFileName, FB_NAMELEN - 1);
        fbNames[n][FB_NAMELEN - 1] = '\0';
        fbSizes[n] = wantDirs ? 0UL : (unsigned long)fd.nFileSizeLow;
        fbIsDir[n] = (char)(wantDirs ? 1 : 0);
        n++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return n - start;
}
void Plat_GetCwd(char *buf, int n) { GetCurrentDirectoryA((DWORD)n, buf); }
void Plat_Chdir(const char *p) { SetCurrentDirectoryA(p); }
int  Plat_AtRoot(const char *cwd) { return strlen(cwd) <= 3; }   /* "C:\" */
#else /* DOS */
int Plat_FbEnum(int wantDirs, const char *filter, int start) {
    struct ffblk ff;
    int n = start, r;
    r = findfirst(wantDirs ? "*.*" : filter, &ff, wantDirs ? FA_DIREC : 0);
    while (r == 0 && n < FB_MAX_FILES - 1) {
        if (wantDirs) {
            if ((ff.ff_attrib & FA_DIREC) && ff.ff_name[0] != '.') {
                strcpy(fbNames[n], ff.ff_name);
                fbSizes[n] = 0UL;
                fbIsDir[n] = 1;
                n++;
            }
        } else if (!(ff.ff_attrib & (FA_DIREC | FA_LABEL))) {
            strcpy(fbNames[n], ff.ff_name);
            fbSizes[n] = (unsigned long)ff.ff_fsize;
            fbIsDir[n] = 0;
            n++;
        }
        r = findnext(&ff);
    }
    return n - start;
}
void Plat_GetCwd(char *buf, int n) {
    if (getcwd(buf, n) == NULL) { buf[0] = '\\'; buf[1] = '\0'; }
}
void Plat_Chdir(const char *p) { chdir(p); }
int  Plat_AtRoot(const char *cwd) { return strlen(cwd) <= 3; }   /* "C:\" */
#endif

/* ---- printer ------------------------------------------------------------ */
FILE *Plat_PrintOpen(void) { return fopen("LPT1", "w"); }
void  Plat_PrintClose(FILE *fp) { fclose(fp); }
#endif /* PP_IN(1) */
#endif

#if PP_IN(4)
void SaveProfiles(void) {
    FILE *fp;
    fp = fopen(ProfilePath, "wb");
    if (fp == NULL) return;
    fwrite(&ProfileCount, sizeof(int), 1, fp);
    fwrite(&ActiveProfile, sizeof(int), 1, fp);
    fwrite(Profiles, sizeof(struct ProgProfile), MAX_PROFILES, fp);
    fclose(fp);
}

void LoadProfiles(void) {
    FILE *fp;
    long fsz, expect;
    fp = fopen(ProfilePath, "rb");
    if (fp == NULL) return; /* no config yet - fine, defaults stay */
    /* Struct sizes differ from the DOS build; ignore a foreign-format file. */
    expect = (long)(2 * sizeof(int) + sizeof(Profiles));
    fseek(fp, 0L, SEEK_END);
    fsz = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    if (fsz != expect) { fclose(fp); return; }
    fread(&ProfileCount, sizeof(int), 1, fp);
    fread(&ActiveProfile, sizeof(int), 1, fp);
    fread(Profiles, sizeof(struct ProgProfile), MAX_PROFILES, fp);
    fclose(fp);
    if (ProfileCount < 0 || ProfileCount > MAX_PROFILES) ProfileCount = 0;
    if (ActiveProfile < -1 || ActiveProfile >= ProfileCount) ActiveProfile = -1;
}

void NewProgrammer(void) {
    struct ProgProfile p;
    int w = 60, h = 18, r, br;
    static const char *baudItems[] = { "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200" };
    static const unsigned long baudVals[8] = { 1200UL, 2400UL, 4800UL, 9600UL, 19200UL, 38400UL, 57600UL, 115200UL };

    if (ProfileCount >= MAX_PROFILES) {
        DlgMessage(" New Programmer ", "Maximum of 8 programmers reached.", "Delete one first.", 1);
        return;
    }

    memset(&p, 0, sizeof(p));

    br = DlgMenu(" Baud Rate ", baudItems, 8);
    if (!br) return;
    p.baud = baudVals[br - 1];

    r = DlgDraw(w, h, " New Programmer Profile ");
    if (!DlgInput(r, w, "Name:", p.name, 20) || p.name[0] == '\0') {
        RedrawScreenFn();
        return;
    }
    DlgInput(r + 2,  w, "Init Str:",   p.initStr,    40);
    DlgInput(r + 4,  w, "Read Cmd:",   p.cmdRead,    20);
    DlgInput(r + 6,  w, "Prog Cmd:",   p.cmdProgram, 20);
    DlgInput(r + 8,  w, "Verify Cmd:", p.cmdVerify,  20);
    DlgInput(r + 10, w, "Blank Cmd:",  p.cmdBlank,   20);

    Profiles[ProfileCount] = p;
    ActiveProfile = ProfileCount;
    ProfileCount++;
    SaveProfiles();

    RedrawScreenFn();
    DlgMessage(" New Programmer ", "Profile saved and selected:", p.name, 0);
}

void EditProgrammer(void) {
    const char *items[MAX_PROFILES];
    struct ProgProfile p;
    int i, n, r, rr, idx, br;
    static const char *baudItems[] = { "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200" };
    static const unsigned long baudVals[8] = { 1200UL, 2400UL, 4800UL, 9600UL, 19200UL, 38400UL, 57600UL, 115200UL };
    int w = 60, h = 18;

    if (ProfileCount == 0) {
        DlgMessage(" Edit Programmer ", "No programmers defined yet.", "Use Setup > New Programmer.", 0);
        return;
    }
    n = 0;
    for (i = 0; i < ProfileCount; i++) items[n++] = Profiles[i].name;
    r = DlgMenu(" Edit Programmer ", items, n);
    if (!r) return;
    idx = r - 1;
    p = Profiles[idx];

    br = DlgMenu(" Baud Rate ", baudItems, 8);
    if (br) p.baud = baudVals[br - 1];

    rr = DlgDraw(w, h, " Edit Programmer Profile ");
    if (!DlgInput(rr, w, "Name:", p.name, 20) || p.name[0] == '\0') {
        RedrawScreenFn();
        return;
    }
    DlgInput(rr + 2,  w, "Init Str:",   p.initStr,    40);
    DlgInput(rr + 4,  w, "Read Cmd:",   p.cmdRead,    20);
    DlgInput(rr + 6,  w, "Prog Cmd:",   p.cmdProgram, 20);
    DlgInput(rr + 8,  w, "Verify Cmd:", p.cmdVerify,  20);
    DlgInput(rr + 10, w, "Blank Cmd:",  p.cmdBlank,   20);

    Profiles[idx] = p;
    SaveProfiles();
    RedrawScreenFn();
    DlgMessage(" Edit Programmer ", "Profile updated:", p.name, 0);
}

void DeleteProgrammer(void) {
    const char *items[MAX_PROFILES];
    int i, n, r, idx;

    if (ProfileCount == 0) {
        DlgMessage(" Delete Programmer ", "No programmers defined.", "", 0);
        return;
    }
    n = 0;
    for (i = 0; i < ProfileCount; i++) items[n++] = Profiles[i].name;
    r = DlgMenu(" Delete Programmer ", items, n);
    if (!r) return;
    idx = r - 1;
    if (!DlgYesNo(" Delete Programmer ", "Delete this profile?")) return;

    for (i = idx; i < ProfileCount - 1; i++) Profiles[i] = Profiles[i + 1];
    ProfileCount--;
    if (ActiveProfile == idx) ActiveProfile = -1;
    else if (ActiveProfile > idx) ActiveProfile--;
    SaveProfiles();
    RedrawScreenFn();
}

void SelectProgrammer(void) {
    const char *items[MAX_PROFILES];
    int i, n, r;

    if (ProfileCount == 0) {
        DlgMessage(" Select Programmer ", "No programmers defined yet.", "Use Setup > New Programmer.", 0);
        return;
    }
    n = 0;
    for (i = 0; i < ProfileCount; i++) items[n++] = Profiles[i].name;
    r = DlgMenu(" Select Programmer ", items, n);
    if (r) {
        ActiveProfile = r - 1;
        SaveProfiles();
    }
}

void GotoAddress(void) {
    char input[10];
    unsigned long addr;
    int w = 50, h = 8, r;

    if (BufferSize == 0) { DlgMessage(" Goto Address ", "Buffer is empty.", "", 0); return; }

    input[0] = '\0';
    r = DlgDraw(w, h, " Goto Address ");
    DlgLine(r, w, "Enter hex offset (ESC to cancel):");
    if (!DlgInput(r + 2, w, "Offset:", input, 8) || input[0] == '\0') return;

    addr = 0;
    sscanf(input, "%lX", &addr);
    if (addr >= BufferSize) addr = BufferSize - 1;

    CursorPos = addr;
    TopLine = CursorPos / BYTES_PER_LINE;
    CursorArea = AREA_HEX;
}

void FindInBuffer(void) {
    char pattern[32];
    unsigned long i, j, pos, start;
    int plen, found;
    int w = 54, h = 9, r;

    if (BufferSize == 0) { DlgMessage(" Find Text ", "Buffer is empty.", "", 0); return; }

    pattern[0] = '\0';
    r = DlgDraw(w, h, " Find Text ");
    DlgLine(r,     w, "Enter ASCII text to search for:");
    DlgLine(r + 1, w, "(searches forward, wraps around)");
    if (!DlgInput(r + 3, w, "Text:", pattern, 30) || pattern[0] == '\0') return;

    plen = strlen(pattern);
    found = 0;
    pos = 0;
    start = CursorPos + 1;
    for (i = 0; i < BufferSize; i++) {
        pos = (start + i) % BufferSize;
        if (pos + (unsigned long)plen > BufferSize) continue;
        for (j = 0; j < (unsigned long)plen; j++) {
            if (DataBuffer[pos + j] != (unsigned char)pattern[j]) break;
        }
        if (j == (unsigned long)plen) { found = 1; break; }
    }

    if (found) {
        CursorPos = pos;
        TopLine = CursorPos / BYTES_PER_LINE;
        CursorArea = AREA_HEX;
    } else {
        DlgMessage(" Find Text ", "Text not found.", "", 0);
    }
}

void FillRange(void) {
    char input1[10], input2[10];
    unsigned long startAddr, endAddr, k;
    int w = 54, h = 11, r;

    if (BufferSize == 0) { DlgMessage(" Fill Range ", "Buffer is empty.", "", 0); return; }

    input1[0] = '\0'; input2[0] = '\0';
    r = DlgDraw(w, h, " Fill Range ");
    DlgLine(r,     w, "Fill a byte range with the fill value");
    DlgLine(r + 1, w, "(set under Options > Fill Byte Value)");
    if (!DlgInput(r + 3, w, "Start (hex offset):", input1, 8) || input1[0] == '\0') return;
    if (!DlgInput(r + 4, w, "End   (hex offset):", input2, 8) || input2[0] == '\0') return;

    startAddr = 0; endAddr = 0;
    sscanf(input1, "%lX", &startAddr);
    sscanf(input2, "%lX", &endAddr);
    if (startAddr >= BufferSize) startAddr = BufferSize - 1;
    if (endAddr >= BufferSize) endAddr = BufferSize - 1;
    if (endAddr < startAddr) { DlgMessage(" Fill Range ", "End must be >= start.", "", 1); return; }

    if (!DlgYesNo(" Fill Range ", "Overwrite this range?")) return;

    for (k = startAddr; k <= endAddr; k++) DataBuffer[k] = FillByte;
    Modified = 1;
}
#endif /* PP_IN(4) */
