// Small command line text editor that will run in a Linux terminal. 

// Resources for this project:
//     VT100.net VT100 User Guide, Chapter 3 Programmer Information:    https://vt100.net/docs/vt100-ug/chapter3.html
//     Wikipedia article on ANSI escape code:                           https://en.wikipedia.org/wiki/ANSI_escape_code

/*
USING COLOR:
The Wikipedia article on ANSI escape codes includes a large table containing all the different argument codes you can 
use with the m command on various terminals. It also includes the ANSI color table with the 8 foreground/background 
colors available.

The first table says we can set the text color using codes 30 to 37, and reset it to the default color using 39. The 
color table says 0 is black, 1 is red, and so on, up to 7 which is white. Putting these together, we can set the text 
color to red using 31 as an argument to the m command. After printing the digit, we use 39 as an argument to m to set 
the text color back to normal.

*/

// Small changes

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>      // needed for iscntrl()
#include <errno.h>      // needed for errno, EAGAIN
#include <fcntl.h>      // needed for open(), O_RDWR, O_CREAT
#include <stdio.h>      // needed for perror(), printf(), sscanf(), snprintf(), FILE, fopen(), getline(), vsnprintf()
#include <stdarg.h>     // needed for va_list, va_start(), and va_end()
#include <stdlib.h>     // Needed for exit(), atexit(), realloc(), free(), malloc()
#include <string.h>     // Needed for memcpy(), strlen(), strup(), memmove(), strerror(), strstr(), memset()
#include <sys/ioctl.h>  // Needed for struct winsize, ioctl(), TIOCGWINSZ 
#include <sys/types.h>  // Needed for ssize_t
#include <termios.h>    // Needed for struct termios, tcsetattr(), TCSAFLUSH, tcgetattr(), BRKINT, ICRNL, INPCK, ISTRIP, 
                        // IXON, OPOST, CS8, ECHO, ICANON, IEXTEN, ISIG, VMIN, VTIME
#include <time.h>       // Needed for time_t, time()
#include <unistd.h>     // Needed for read(), STDIN_FILENO, write(), STDOUT_FILENO, ftruncate(), close()

/*** defines ***/

#define FALSE              0
#define TRUE               1

#define TEXT_ED_VERSION    "0.0.1"
#define TAB_STOP           8
#define QUIT_TIMES         3

#define CTRL_KEY(k)        ((k) & 0x1F)  // unset the upper 3 bits of k

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DELETE_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

enum editorHighlight {  // enum of highlight colors
  HL_NORMAL = 0,
  HL_NUMBER,
  HL_MATCH
};

/*** data ***/

typedef unsigned char   byte;
typedef unsigned char   boolean;

typedef struct textRow {  // the typedef lets us refer to the type as "textRow" instead of "struct textRow"
  int size;  // the quantity of elements in the *chars array
  int rsize;  // the quantity of elements in the *render array
  char *chars;  // pointer to a dynamically allocated array that holds all the characters in a single row of text as read from a file
  char *render;  // pointer to a dynamically allocated array that holds all the characters in a single row of text as they are displayed on the screen
  unsigned char *hl;  // "highlight" - an array to store the highlighting characteristics of each character
} textRow;  // textRow stands for "editor row" - it stores a line of text as a pointer to the dynamically-allocated character data and a length

struct editorConfig {
  int cursorXCoord;  // cursorXCoord - horizontal index into the chars field of textRow (cursor location???)
  int cursorYCoord;  // cursorXCoord - horizontal index into the chars field of textRow (cursor location???)
  int renderXCoord;  //horizontal index into the render field of textRow - if there are no tab son the current line, renderXCoord will be the same as cursorXCoord 
  int rowOffset;  // row offset - keeps track of what row of the file the user is currently scrolled to
  int columnOffset;  // column offset - keeps track of what column of the file the user is currently scrolled to
  int screenRows;  // qty of rows on the screen - window size
  int screenColumns;  // qty of columns on the screen - window size
  int totalRows;  // the number of rows (lines) of text being displayed/stored by the editor
  textRow *row;  // Hold a single row of test, both as read from a file, and as displayed on the screen
  boolean modified;  // modified flag - We call a text buffer “modified” if it has been modified since opening or saving the file - used to keep track of whether the text loaded in our editor differs from what’s in the file
  char *filename;  // Name of the file being edited
  char statusMsg[80];  // holds an 80 character message to the user displayed on the status bar.
  time_t statusMsg_time;  // timestamp for the status message - current status message will only display for five seconds or until the next key is pressed since the screen in refreshed only when a key is pressed.
  struct termios originalTerminalAttributes;  // structure to hold the original state of the terminal when our program began, before we started altering its state
};

struct editorConfig Text;

/*** prototypes ***/
// 8888888b.  8888888b.   .d88888b. 88888888888 .d88888b. 88888888888 Y88b   d88P 8888888b.  8888888888 .d8888b.  
// 888   Y88b 888   Y88b d88P" "Y88b    888    d88P" "Y88b    888      Y88b d88P  888   Y88b 888       d88P  Y88b 
// 888    888 888    888 888     888    888    888     888    888       Y88o88P   888    888 888       Y88b.      
// 888   d88P 888   d88P 888     888    888    888     888    888        Y888P    888   d88P 8888888    "Y888b.   
// 8888888P"  8888888P"  888     888    888    888     888    888         888     8888888P"  888           "Y88b. 
// 888        888 T88b   888     888    888    888     888    888         888     888        888             "888 
// 888        888  T88b  Y88b. .d88P    888    Y88b. .d88P    888         888     888        888       Y88b  d88P 
// 888        888   T88b  "Y88888P"     888     "Y88888P"     888         888     888        8888888888 "Y8888P" 

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/
// 88888888888 8888888888 8888888b.  888b     d888 8888888 888b    888        d8888 888      
//     888     888        888   Y88b 8888b   d8888   888   8888b   888       d88888 888      
//     888     888        888    888 88888b.d88888   888   88888b  888      d88P888 888      
//     888     8888888    888   d88P 888Y88888P888   888   888Y88b 888     d88P 888 888      
//     888     888        8888888P"  888 Y888P 888   888   888 Y88b888    d88P  888 888      
//     888     888        888 T88b   888  Y8P  888   888   888  Y88888   d88P   888 888      
//     888     888        888  T88b  888   "   888   888   888   Y8888  d8888888888 888      
//     888     8888888888 888   T88b 888       888 8888888 888    Y888 d88P     888 88888888 

#define CLEAR_SCREEN         "\x1b[2J" 
#define CLEAR_SCREEN_SIZE    4
#define CURSOR_HOME          "\x1b[H"
#define CURSOR_HOME_SIZE     3

// -----------------------------------------------------------------------------
void die(const char *string)  // prints error message and exits program with error code 1
{
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SIZE);  // clear the screen
  write(STDOUT_FILENO, CURSOR_HOME, CURSOR_HOME_SIZE);  // position the cursor at the top left of the screen

  perror(string);  // looks at the global errno variable and prints a decriptive error message for it preceeded by the string given to it
  exit(1);
}

// -----------------------------------------------------------------------------
void disableRawMode()  // disable raw text input mode
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &Text.originalTerminalAttributes) == -1) {  // set the terminal's attribute flags to their original states
    die("tcsetattr");                                             // and exit program if there is an error
  }
}

// -----------------------------------------------------------------------------
void enableRawMode()  // enable raw text input mode
{
  if (tcgetattr(STDIN_FILENO, &Text.originalTerminalAttributes) == -1) {  // read the terminal's attribute flags into Text.originalTerminalAttributes
    die("tcgetattr");                                  // and exit program if there is an error
  }
  atexit(disableRawMode);  // call the disableRawMode function automatically upon exit from the program form either return from main() 
                           // or using exit()

  struct termios rawInputAttributes = Text.originalTerminalAttributes;
  rawInputAttributes.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);  // unset the input bitflags for enabling Ctrl-C, enabling Ctrl-M, enabling 
                                                             // parity checking, stripping the 8th bit of input, enabling Ctrl-S and Ctrl-Q
  rawInputAttributes.c_oflag &= ~(OPOST);  // unset the output flag for automatically prefixing a new line character with a carriage retrun character
  rawInputAttributes.c_cflag |= (CS8);  // set all the bits for character size to 8 bits per byte
  rawInputAttributes.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);  // unset the loacal bitflags for echoing to the terminal, canonical mode, enabling 
                                                    // Ctrl-V, enabling Ctrl-C and Ctrl-Z
  rawInputAttributes.c_cc[VMIN] = 0;  // sets the minimum number of bytes of input needed for read() to return to 0 - read can return as soon as there 
                       // is any input
  rawInputAttributes.c_cc[VTIME] = 1;  // sets the maximum amount of time to wait befoore read() returns to 1/10 of a second (100 milliseconds) - if 
                        // read() times out, it returns 0, which makes sense because its usual return value in the number of bytes read
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawInputAttributes) == -1 ) {  // set the terminal's attribute flags to those in raw
    die("tcsetattr");                                     // and exit program if there is an error
  }
}

// -----------------------------------------------------------------------------
// waits for one keypress and returns it
int editorReadKey() 
{ 
  int nread;
  char keypress;
  while ((nread = read(STDIN_FILENO, &keypress, 1)) == 0) {  // read 1 byte from standard input (keyboard) into c
    if (nread == -1 && errno != EAGAIN) die("read");  // and exit program if there is an error
  }

  if (keypress == '\x1b') {  // if c is an escape charater
    char seq[3];

    // read the next 2 bytes into seq
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';  //--- These two reads are the reason I have
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';  //--- to press the escape button 3 times???
                                                             //--- because they don't time out???
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DELETE_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {  // Arrow key was pressed
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return keypress;
  }
}

// -----------------------------------------------------------------------------
int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }

  
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
    i++;
  }
  buf[i] = '\0';  // terminate the string in buf with a null character

  if (buf[0] != '\x1b' || buf[1] != '[') {  // check that buf[] contains an escape sequence, if not return -1 (error)
    return -1;
  }
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {  // copy the contents of buf[2], buf[3] into rows, cols respectively 
    return -1;
  }

  return 0;
}

// -----------------------------------------------------------------------------
// get the size of the terminal window
int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, & ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12 ) {  // writes 2 escape sequence characters, the first (C command) moves the
      return -1;  // Error                                        // cursor 999 spaces to the right, the second moves it 999 spaces down
    }
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** syntax highlighting ***/
// 888    888 8888888 .d8888b.  888    888 888      8888888 .d8888b.  888    888 88888888888 
// 888    888   888  d88P  Y88b 888    888 888        888  d88P  Y88b 888    888     888     
// 888    888   888  888    888 888    888 888        888  888    888 888    888     888     
// 8888888888   888  888        8888888888 888        888  888        8888888888     888     
// 888    888   888  888  88888 888    888 888        888  888  88888 888    888     888     
// 888    888   888  888    888 888    888 888        888  888    888 888    888     888     
// 888    888   888  Y88b  d88P 888    888 888        888  Y88b  d88P 888    888     888     
// 888    888 8888888 "Y8888P88 888    888 88888888 8888888 "Y8888P88 888    888     888   


// -----------------------------------------------------------------------------
//
void editorUpdateSyntax(textRow *row) {
  row->hl = realloc(row->hl, row->rsize);  // First we realloc() the needed memory, since this might be a new row or the row might be bigger than the last time we highlighted it.
  memset(row->hl, HL_NORMAL, row->rsize);  // use memset() to set all characters to HL_NORMAL by default

  int i;
  for (i = 0; i < row->size; i++) {  // loop through the characters
    if (isdigit(row->render[i])) {
      row->hl[i] = HL_NUMBER;        // set the digits to HL_NUMBER
    }
  }
}

// -----------------------------------------------------------------------------
//
int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_NUMBER: return 31;  // return the ANSI code for "foreground red"
    case HL_MATCH: return 33;   // return the ANSI code for "foreground blue"
    default: return 37;         // return the ANSI code for "foreground white"
  }
}


/*** row operations ***/
// 8888888b.   .d88888b.  888       888       .d88888b.  8888888b.  8888888888 8888888b.         d8888 88888888888 8888888 .d88888b.  888b    888  .d8888b.  
// 888   Y88b d88P" "Y88b 888   o   888      d88P" "Y88b 888   Y88b 888        888   Y88b       d88888     888       888  d88P" "Y88b 8888b   888 d88P  Y88b 
// 888    888 888     888 888  d8b  888      888     888 888    888 888        888    888      d88P888     888       888  888     888 88888b  888 Y88b.      
// 888   d88P 888     888 888 d888b 888      888     888 888   d88P 8888888    888   d88P     d88P 888     888       888  888     888 888Y88b 888  "Y888b.   
// 8888888P"  888     888 888d88888b888      888     888 8888888P"  888        8888888P"     d88P  888     888       888  888     888 888 Y88b888     "Y88b. 
// 888 T88b   888     888 88888P Y88888      888     888 888        888        888 T88b     d88P   888     888       888  888     888 888  Y88888       "888 
// 888  T88b  Y88b. .d88P 8888P   Y8888      Y88b. .d88P 888        888        888  T88b   d8888888888     888       888  Y88b. .d88P 888   Y8888 Y88b  d88P 
// 888   T88b  "Y88888P"  888P     Y888       "Y88888P"  888        8888888888 888   T88b d88P     888     888     8888888 "Y88888P"  888    Y888  "Y8888P" 

// -----------------------------------------------------------------------------
// converts a chars index into a render index
int editorRowCxToRx(textRow *row, int cursorXCoord) {
  int renderXCoord = 0;
  int j;
  for (j = 0; j < cursorXCoord; j++) {
    if (row->chars[j] == '\t')
      renderXCoord += (TAB_STOP - 1) - (renderXCoord % TAB_STOP);
    renderXCoord++;
  }
  return renderXCoord;
}

// -----------------------------------------------------------------------------
// converts a render index into a chars index
// To convert an renderXCoord into a cursorXCoord, we do pretty much the same thing when converting the other way: loop through the chars string, calculating the current renderXCoord value (cur_renderXCoord) as we go. But instead of stopping when we hit a particular cursorXCoord value and returning cur_renderXCoord, we want to stop when cur_renderXCoord hits the given renderXCoord value and return cursorXCoord
int editorRowRxToCx(textRow *row, int renderXCoord) {
  int cur_renderXCoord = 0;
  int cursorXCoord;
  for (cursorXCoord = 0; cursorXCoord < row->size; cursorXCoord++) {
    if (row->chars[cursorXCoord] == '\t')
      cur_renderXCoord += (TAB_STOP - 1) - (cur_renderXCoord % TAB_STOP);
    cur_renderXCoord++;

    if (cur_renderXCoord > renderXCoord) return cursorXCoord;  // should handle all renderXCoord values that are valid indexes into render
  }
  return cursorXCoord;  // just in case the caller provided an renderXCoord that’s out of range, which shouldn’t happen
}

// -----------------------------------------------------------------------------
// uses the chars string of an textRow to fill in the contents of the render string - copy each character from chars to render
void editorUpdateRow(textRow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

// -----------------------------------------------------------------------------
// allocates memory space for a new textRow, make space at any position in the Text.row array, then copies the given string to it 
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > Text.totalRows) return;  // validate at index is within range

  Text.row = realloc(Text.row, sizeof(textRow) * (Text.totalRows + 1));  // allocate memory for a new row
  memmove(&Text.row[at + 1], &Text.row[at], sizeof(textRow) * (Text.totalRows - at)); // shift all rows after the index at down by one to make room for new row at at

  Text.row[at].size = len;
  Text.row[at].chars = malloc(len + 1);
  memcpy(Text.row[at].chars, s, len);
  Text.row[at].chars[len] = '\0';

  Text.row[at].rsize = 0;
  Text.row[at].render = NULL;
  Text.row[at].hl = NULL;
  editorUpdateRow(&Text.row[at]);

  Text.totalRows++;
  Text.modified = TRUE;  // why not just set it to true? - and then set to false when save file
}

// -----------------------------------------------------------------------------
//  frees memory owned by a row
void editorFreeRow(textRow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

// -----------------------------------------------------------------------------
// editorDelRow() looks a lot like editorRowDelChar(), because in both cases we are deleting a single element from an array of elements by its index.
void editorDelRow(int at) {
  if (at < 0 || at >= Text.totalRows) return;  // validate at index
  editorFreeRow(&Text.row[at]);  // free the memory owned by the row using editorFreeRow()
  memmove(&Text.row[at], &Text.row[at + 1], sizeof(textRow) * (Text.totalRows - at - 1));  // use memmove() to overwrite the deleted row struct with the rest of the rows that come after it
  Text.totalRows--;  
  Text.modified = TRUE;
}

// -----------------------------------------------------------------------------
// textRow *row - pointer to an textRow struct
// int at - the index at which we want to insert the character (index to insert 'at'???)
// int c - new character to insert
void editorRowInsertChar(textRow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;  // validate at - Notice that at is allowed to go one character past the end of the string, in which case the character should be inserted at the end of the string.
  row->chars = realloc(row->chars, row->size + 2);  // Then we allocate one more byte for the chars of the textRow (we add 2 because we also have to make room for the null byte)
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);  // use memmove() to make room for the new character. move everything from index at to the end of the string over one character leaving a "hole" at index at.  memmove() comes from <string.h>. It is like memcpy(), but is safe to use when the source and destination arrays overlap.
  row->size++;  // increment the size of the chars array, 
  row->chars[at] = c;  // assign the character to its position in the array
  editorUpdateRow(row);  // call editorUpdateRow() so that the render and rsize fields get updated with the new row content.
  Text.modified = TRUE;  // why not just set it to true? - and then set to false when save file
}

// -----------------------------------------------------------------------------
// appends a string to the end of a row
void editorRowAppendString(textRow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);  // The row’s new size is row->size + len + 1 (including the null byte), so first we allocate that much memory for row->chars
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  Text.modified = TRUE;
}

// -----------------------------------------------------------------------------
// textRow *row - pointer to an textRow struct
// int at - the index at which we want to insert the character (index to insert 'at'???)
void editorRowDelChar(textRow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  Text.modified = TRUE;
}

/*** editor operations ***/  // This section will contain functions that we’ll call from editorProcessKeypress() when we’re mapping keypresses to various text editing operations
// 8888888888 8888888b. 8888888 88888888888 .d88888b.  8888888b.        .d88888b.  8888888b.  8888888888 8888888b.         d8888 88888888888 8888888 .d88888b.  888b    888  .d8888b.  
// 888        888  "Y88b  888       888    d88P" "Y88b 888   Y88b      d88P" "Y88b 888   Y88b 888        888   Y88b       d88888     888       888  d88P" "Y88b 8888b   888 d88P  Y88b 
// 888        888    888  888       888    888     888 888    888      888     888 888    888 888        888    888      d88P888     888       888  888     888 88888b  888 Y88b.      
// 8888888    888    888  888       888    888     888 888   d88P      888     888 888   d88P 8888888    888   d88P     d88P 888     888       888  888     888 888Y88b 888  "Y888b.   
// 888        888    888  888       888    888     888 8888888P"       888     888 8888888P"  888        8888888P"     d88P  888     888       888  888     888 888 Y88b888     "Y88b. 
// 888        888    888  888       888    888     888 888 T88b        888     888 888        888        888 T88b     d88P   888     888       888  888     888 888  Y88888       "888 
// 888        888  .d88P  888       888    Y88b. .d88P 888  T88b       Y88b. .d88P 888        888        888  T88b   d8888888888     888       888  Y88b. .d88P 888   Y8888 Y88b  d88P 
// 8888888888 8888888P" 8888888     888     "Y88888P"  888   T88b       "Y88888P"  888        8888888888 888   T88b d88P     888     888     8888888 "Y88888P"  888    Y888  "Y8888P"

// -----------------------------------------------------------------------------
// take a character and use editorRowInsertChar() to insert that character into the position that the cursor is at.
void editorInsertChar(int c) {
  if (Text.cursorYCoord == Text.totalRows) {    // if the cursor is located on the very last line of the editor text (the cursor is on the tilde line after the end of the file, so we need to append a new row)
    editorInsertRow(Text.totalRows, "", 0);  // allocate memory space for a new row - the new character will be inserted on a new row 
  }
  editorRowInsertChar(&Text.row[Text.cursorYCoord], Text.cursorXCoord, c);  // insert the character (c) into the specific row of the row array
  Text.cursorXCoord++;  // move the cursor forward so that the next character the user inserts will go after the character just inserted
}

// -----------------------------------------------------------------------------
//
void editorInsertNewline() {
  if (Text.cursorXCoord == 0) {  // if the cursor is at the beginning of a line
    editorInsertRow(Text.cursorYCoord, "", 0);  // insert a new blank row before the line the cursor is on
  } else {  // Otherwise, we have to split the line we’re on into two rows
    textRow *row = &Text.row[Text.cursorYCoord];  // assign a new pointer to the address of the first character of the text that will be moved down a row
    editorInsertRow(Text.cursorYCoord + 1, &row->chars[Text.cursorXCoord], row->size - Text.cursorXCoord); // insert the text pointed to by row into a new line - calls editorUpdateRow()
    row = &Text.row[Text.cursorYCoord]; // reset row to the line that was truncated, because editorInsertRow() may have invalidated the pointer
    row->size = Text.cursorXCoord;  // set the size equal to the x coordinate
    row->chars[row->size] = '\0'; // add a null character to terminate the string in row->chars
    editorUpdateRow(row);  // update the row that was truncated
  }
  Text.cursorYCoord++;    // move the cursor to the beginning of the new row
  Text.cursorXCoord = 0;  // move the cursor to the beginning of the new row
}

// -----------------------------------------------------------------------------
// use editorRowDelChar() to delete a character at the position that the cursor is at.
void editorDelChar() {
  if (Text.cursorYCoord == Text.totalRows) return;  // do nothing if the cursor is on the tilde line - If the cursor’s past the end of the file, then there is nothing to delete, and we return immediately
  if (Text.cursorXCoord == 0 && Text.cursorYCoord == 0) return;  //do nothing if the cursor is at the beginning of the first line

  // Otherwise, we get the textRow the cursor is on, and if there is a character to the left of the cursor, we delete it and move the cursor one to the left
  textRow *row = &Text.row[Text.cursorYCoord];
  if (Text.cursorXCoord > 0) {
    editorRowDelChar(row, Text.cursorXCoord - 1);
    Text.cursorXCoord--;
  } else {  // if (Text.cursorXCoord == 0) - if the cursor is at the beginning of the line of text, append the entire line to the previous line and reduce the size of
    Text.cursorXCoord = Text.row[Text.cursorYCoord - 1].size;  // move the x coordniate of the cursor to the end of the previous row (while staying in the same row)
    editorRowAppendString(&Text.row[Text.cursorYCoord - 1], row->chars, row->size);  // Append the contents of the line the cursor is on to the contents of the line above it
    editorDelRow(Text.cursorYCoord);  // delete the row the cursor is on
    Text.cursorYCoord--;  // move the cursor up one row to the position where the two lines were joined
  }
}

/*** file i/o ***/
// 8888888888 8888888 888      8888888888      8888888        d88P  .d88888b.  
// 888          888   888      888               888         d88P  d88P" "Y88b 
// 888          888   888      888               888        d88P   888     888 
// 8888888      888   888      8888888           888       d88P    888     888 
// 888          888   888      888               888      d88P     888     888 
// 888          888   888      888               888     d88P      888     888 
// 888          888   888      888               888    d88P       Y88b. .d88P 
// 888        8888888 88888888 8888888888      8888888 d88P         "Y88888P"  
                                                                                                                                                       
// -----------------------------------------------------------------------------
//
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < Text.totalRows; j++)  // add up the lengths of each row of text, adding 1 to each one for the newline character we’ll add to the end of each line
    totlen += Text.row[j].size + 1;
  *buflen = totlen;  // save the total length into buflen, to tell the caller how long the string is

  char *buf = malloc(totlen);  // allocate the required memory for the string
  char *p = buf;  // set p to point at the same address as buf - this is so we increase the address p is pointing to as we copy characters while leaving buf point to the beginning address
  for (j = 0; j < Text.totalRows; j++) {  // loop through the rows
    memcpy(p, Text.row[j].chars, Text.row[j].size);  // memcpy() the contents of each row to the end of the buffer
    p += Text.row[j].size;  // advance the address being pointed at by the p pointer by the qty of characters added to the string
    *p = '\n';  // append a new line character after each row
    p++;  // advance the address being pointed at by the pointer by one to account for the newline character
  }

  return buf; // return the string - we expect the caller to free the memory
}

// -----------------------------------------------------------------------------
// for opening and reading a file from disk
void editorOpen(char *filename) {
  free(Text.filename);
  Text.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || 
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(Text.totalRows, line, linelen);
  }
  free(line);
  fclose(fp);
  Text.modified = FALSE;  // reset the modified flag
}

// -----------------------------------------------------------------------------
// saves the current text on the screen to the file with error handling
void editorSave() {
  if (Text.filename == NULL) {
    Text.filename = editorPrompt("Save as: %s (ESC x 3 to cancel)", NULL);
    if (Text.filename == NULL) {  // pressing ESC causes editorPrompt to return NULL
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);  // converts the contents of rows array into one continuos string

  int fd = open(Text.filename, O_RDWR | O_CREAT, 0644);  // create a new file if it doesn’t already exist (O_CREAT), and we want to open it for reading and writing (O_RDWR) - 0644 is the standard permissions you usually want for text files
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {  // sets the file’s size to the specified length
      if (write(fd, buf, len) == len) {  // write the contents of buf to the file referenced by fd
        close(fd);
        free(buf);
        Text.modified = FALSE;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/
//  .d8888b.  8888888888        d8888 8888888b.   .d8888b.  888    888 
// d88P  Y88b 888              d88888 888   Y88b d88P  Y88b 888    888 
// Y88b.      888             d88P888 888    888 888    888 888    888 
//  "Y888b.   8888888        d88P 888 888   d88P 888        8888888888 
//     "Y88b. 888           d88P  888 8888888P"  888        888    888 
//       "888 888          d88P   888 888 T88b   888    888 888    888 
// Y88b  d88P 888         d8888888888 888  T88b  Y88b  d88P 888    888 
//  "Y8888P"  8888888888 d88P     888 888   T88b  "Y8888P"  888    888 

// -----------------------------------------------------------------------------
//
void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;  // static variable to know which line’s hl needs to be restored
  static char *saved_hl = NULL;  // dynamically allocated array which points to NULL when there is nothing to restore

  if (saved_hl) {  // if there is something to restore
    memcpy(Text.row[saved_hl_line].hl, saved_hl, Text.row[saved_hl_line].rsize);  // memcpy it to the saved line’s hl
    free(saved_hl);  // deallocate saved_hl
    saved_hl = NULL;  // set it back to NULL - so we won't have a dangling pointer
  }

  if (key == '\r' || key == '\x1b') {  // check if the user pressed Enter or Escape
    last_match = -1;  // return to defaults - no last match
    direction = 1;  // return to defaults - forward search
    return;  // we return immediately instead of doing another search
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  // Otherwise, after any other keypress, we do another search for the current query string
  if (last_match == -1) direction = 1;
  int current = last_match;  // current is the index of the current row we are searching
  int i;
  for (i = 0; i < Text.totalRows; i++) {  // loop through all the row structures in the E structure
    current += direction;  //  move current index forward or backward based on the value of direction
    if (current == -1) current = Text.totalRows - 1;  // if beginning of the text is reached, wrap around to the last row
    else if (current == Text.totalRows) current = 0;  // if the end of the text is reached, wrap around to the first row

    textRow *row = &Text.row[current];  // set a pointer to the address of Text.row[i]
    char *match = strstr(row->render, query);  // searches the row structure pointed to by row->render for the first occurence of query
    if (match) {  // a match is found
      last_match = current;
      Text.cursorYCoord = current;  // set cursor to location of the match
      Text.cursorXCoord = editorRowRxToCx(row, match - row->render); // set cursor to location of the match converted from a render index to a chars index
      Text.rowOffset = Text.totalRows;  // scroll the text row where the match was found to the top of the screen -  set Text.rowOffset so that we are scrolled to the very bottom of the file, which will cause editorScroll() to scroll upwards at the next screen refresh so that the matching line will be at the very top of the screen
      
      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

// -----------------------------------------------------------------------------
//
void editorFind() {
  int saved_cursorXCoord = Text.cursorXCoord;          // change all these into a cursor position struct
  int saved_cursorYCoord = Text.cursorYCoord;          // ...
  int saved_columnOffset = Text.columnOffset;  // ...
  int saved_rowOffset = Text.rowOffset;  // ...

  char *query = editorPrompt("Search: %s (Use ESCx3/Arrows/Enter)", 
                             editorFindCallback);  // prompt for search text
  
  if (query) {  
    free(query);
  } else {  // if query is NULL, that means they pressed escape, so restore the saved values
    Text.cursorXCoord = saved_cursorXCoord;
    Text.cursorYCoord = saved_cursorYCoord;
    Text.columnOffset = saved_columnOffset;
    Text.rowOffset = saved_rowOffset;
  }
}


/*** append buffer ***/
//        d8888 8888888b.  8888888b.  8888888888 888b    888 8888888b.       888888b.   888     888 8888888888 8888888888 8888888888 8888888b.  
//       d88888 888   Y88b 888   Y88b 888        8888b   888 888  "Y88b      888  "88b  888     888 888        888        888        888   Y88b 
//      d88P888 888    888 888    888 888        88888b  888 888    888      888  .88P  888     888 888        888        888        888    888 
//     d88P 888 888   d88P 888   d88P 8888888    888Y88b 888 888    888      8888888K.  888     888 8888888    8888888    8888888    888   d88P 
//    d88P  888 8888888P"  8888888P"  888        888 Y88b888 888    888      888  "Y88b 888     888 888        888        888        8888888P"  
//   d88P   888 888        888        888        888  Y88888 888    888      888    888 888     888 888        888        888        888 T88b   
//  d8888888888 888        888        888        888   Y8888 888  .d88P      888   d88P Y88b. .d88P 888        888        888        888  T88b  
// d88P     888 888        888        8888888888 888    Y888 8888888P"       8888888P"   "Y88888P"  888        888        8888888888 888   T88b 


struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT { NULL, 0}

// -----------------------------------------------------------------------------
void abAppend(struct abuf *ab, const char *s, int len) 
{
  char *new = realloc(ab->b, ab->len + len);  // allocate memeory for the new string

  if (new == NULL) {
    return;
  }
  memcpy(&new[ab->len], s, len);  // copy the contents of s onto the end of the contents of new
  ab->b = new;  // update ab.b
  ab->len += len;  // update ab.len
}

// -----------------------------------------------------------------------------
void abFree(struct abuf *ab) 
{
  free(ab->b);
  // comment just so I can collapse this function
}

/*** output ***/
//  .d88888b.  888     888 88888888888 8888888b.  888     888 88888888888 
// d88P" "Y88b 888     888     888     888   Y88b 888     888     888     
// 888     888 888     888     888     888    888 888     888     888     
// 888     888 888     888     888     888   d88P 888     888     888     
// 888     888 888     888     888     8888888P"  888     888     888     
// 888     888 888     888     888     888        888     888     888     
// Y88b. .d88P Y88b. .d88P     888     888        Y88b. .d88P     888     
//  "Y88888P"   "Y88888P"      888     888         "Y88888P"      888     

// -----------------------------------------------------------------------------
//  check if the cursor has moved outside of the visible window, and if so, adjust Text.rowOffset so that the cursor is just inside the visible window.
void editorScroll() {
  Text.renderXCoord = 0;
  if (Text.cursorYCoord < Text.totalRows) {
    Text.renderXCoord = editorRowCxToRx(&Text.row[Text.cursorYCoord], Text.cursorXCoord);
  }

  if (Text.cursorYCoord < Text.rowOffset) {
    Text.rowOffset = Text.cursorYCoord;
  }
  if (Text.cursorYCoord >= Text.rowOffset + Text.screenRows) {
    Text.rowOffset = Text.cursorYCoord - Text.screenRows + 1;
  }
  if (Text.renderXCoord < Text.columnOffset) {
    Text.columnOffset = Text.renderXCoord;
  }
  if (Text.renderXCoord >= Text.columnOffset + Text.screenColumns) {
    Text.columnOffset = Text.renderXCoord - Text.screenColumns + 1;
  }
}

// -----------------------------------------------------------------------------
// draws a tilde on every row of the terminal just like Vim
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < Text.screenRows; y++) {
    int filtextRow = y + Text.rowOffset; // To get the row of the file that we want to display at each y position, we add Text.rowOffset to the y position.
    if (filtextRow >= Text.totalRows) {
      if (Text.totalRows == 0 && y == Text.screenRows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Text Editor -- version %s", TEXT_ED_VERSION);
        if (welcomelen > Text.screenColumns) {
          welcomelen = Text.screenColumns;
        }
        int padding = (Text.screenColumns - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome,welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = Text.row[filtextRow].rsize - Text.columnOffset;
      if (len < 0) len = 0;
      if (len > Text.screenColumns) len = Text.screenColumns;
      char *c = &Text.row[filtextRow].render[Text.columnOffset];
      unsigned char *hl = &Text.row[filtextRow].hl[Text.columnOffset];  // a pointer, hl, to the slice of the hl array that corresponds to the slice of render that we are printing
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {  // for every character 
        if (hl[j] == HL_NORMAL) {  // if the character gets normal highlighting
          if (current_color != -1) {
            abAppend(ab, "\x1b[m", 3); /*---*/ // TB: append an escape sequence for NOT inverted coloring 
            abAppend(ab, "\x1b[39m", 5);  // append an escaspe sequence for NORMAL coloring
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);  // append the character
        } else {  // if the character does not get normal highlighting 
          int color = editorSyntaxToColor(hl[j]);  // get color to highlight
    /*-*/ if (hl[j] == HL_MATCH) {  // TB:
            abAppend(ab, "\x1b[7m", 4);  // append an escape sequence to invert colors
          } /*-*/
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }  
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[m", 3); /*---*/ // TB: append an escape sequence for NOT inverted coloring 
      abAppend(ab, "\x1b[39m", 5);  // after we’re done looping through all the characters and displaying them, we print a final <esc>[39m escape sequence to make sure the text color is reset to default
    }

    abAppend(ab, "\x1b[K", 3);  // append a 3-byte escape sequence which erases the line right of the cursor
                                  // "\x1b[2K" - erases whole line
                                  // "\x1b[1K" - erases line to the left of the cursor
                                  // "\x1b[0K" - erases line to the rightt of the cursor (same as "\x1b[K")
    abAppend(ab, "\r\n", 2);
  }
}

// -----------------------------------------------------------------------------
void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);   // The escape sequence <esc>[7m switches to inverted colors
                                // The m command (Select Graphic Rendition) causes the text printed after it to be printed with various 
                                // possible attributes including bold (1), underscore (4), blink (5), and inverted colors (7)
                                // For example, you could specify all of these attributes using the command <esc>[1;4;5;7m. An argument 
                                // of 0 clears all attributes, and is the default argument, so we use <esc>[m to go back to normal text formatting.
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    Text.filename ? Text.filename : "[No Name]", Text.totalRows, 
    Text.modified ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    Text.cursorYCoord + 1, Text.totalRows);
  if (len > Text.screenColumns) len = Text.screenColumns;
  abAppend(ab, status, len);
  while (len < Text.screenColumns) {
    if (Text.screenColumns - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);  // The escape sequence <esc>[m switches back to normal formatting
  abAppend(ab, "\r\n", 2);
}

// -----------------------------------------------------------------------------
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);  // clear the message bar
  int msglen = strlen(Text.statusMsg);
  if (msglen > Text.screenColumns) msglen = Text.screenColumns;  // if message length is grater than the width of the screen, cut it down
  if (msglen && time(NULL) - Text.statusMsg_time < 5)  // display the message only if it is lenn than 5 seconds old
    abAppend(ab, Text.statusMsg, msglen);
}

// -----------------------------------------------------------------------------
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);  // set mode escape sequence - hide cursor
  // abAppend(&ab, "\x1b[2J", 4); REMOVED
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (Text.cursorYCoord - Text.rowOffset) + 1,
                                            (Text.renderXCoord - Text.columnOffset) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);  // reset mode escape sequence - show cursor
  
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

#if 0
// -----------------------------------------------------------------------------
// clear screen, draw tildes, and position cursor at top-left
void editorRefreshScreen()
{
  struct abuf ab = ABUF_INIT;  // initialize a new empty buffer, ab

  abAppend(&ab, "\x1b[2J", 4);    // Appends a 4-byte escape sequence (\x1b, [, 2, J ) to the buffer which will clear the screen 
                                         // \x1b is the escape character and "J" commands are used for erasing in the display 
  abAppend(&ab, "\x1b[H", 3);  // Appends a 3-byte escape sequence (\x1b, [, H ) to the buffer which will reposition the cursor
                                         // "H" commands take two parameters which default to 1 each if not specified
                                         // If you have an 80x24 size terminal \x1b[12;40H would move the cursor to the center of the screen
                                         // Escape sequence documentation: https://vt100.net/docs/vt100-ug/chapter3.html
                                         // More info on VT100: https://en.wikipedia.org/wiki/VT100    
                                         // Other terminals: https://en.wikipedia.org/wiki/Ncurses & https://en.wikipedia.org/wiki/Terminfo 
  editorDrawRows(&ab);  // append tildes

  abAppend(&ab, "\x1b[H", 3);  // append another 3-byte escape sequence (\x1b, [, H ) to the buffer which will reposition the cursor

   write(STDOUT_FILENO, ab.b, ab.len);  // wriet the contents of the buffer to the terminal
   abFree(&ab);  // deallocate the buffer memory
}
#endif

// -----------------------------------------------------------------------------
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(Text.statusMsg, sizeof(Text.statusMsg), fmt, ap);
  va_end(ap);
  Text.statusMsg_time = time(NULL);
}

/*** input ***/
// 8888888 888b    888 8888888b.  888     888 88888888888 
//   888   8888b   888 888   Y88b 888     888     888     
//   888   88888b  888 888    888 888     888     888     
//   888   888Y88b 888 888   d88P 888     888     888     
//   888   888 Y88b888 8888888P"  888     888     888     
//   888   888  Y88888 888        888     888     888     
//   888   888   Y8888 888        Y88b. .d88P     888     
// 8888888 888    Y888 888         "Y88888P"      888       

// -----------------------------------------------------------------------------
//
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {  // The prompt is expected to be a format string containing a %s, which is where the user’s input will be displayed.
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {  // infinite loop that repeatedly sets the status message, refreshes the screen, and waits for a keypress to handle
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();  // read user input
    if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {  // escape key
      editorSetStatusMessage("");  // erase status message asking for a file name
      if (callback) callback(buf, c);  // the if (callback) allows the caller to pass NULL for the callback, in case they don't want to use the callback
      free(buf); // free the memory allocated to buf
      return NULL;  // end loop and return without file name
    } else if (c == '\r') {  // if Enter is pressed
      if (buflen != 0) {  // if the length of the buffer where we are storing the text is not 0 - meaning the buffer is not empty
        editorSetStatusMessage("");  // set status message back to nothing
        if (callback) callback(buf, c);  // the if (callback) allows the caller to pass NULL for the callback, in case they don't want to use the callback
        return buf;  // return the file name entered
      }
    } else if (!iscntrl(c) && c < 128) {  // Otherwise, when they input a printable character (not a control character and not a charcter value above 128 - so no characters in our editorKey enum), we append it to buf - Notice that we have to make sure the input key isn’t one of the special keys in the editorKey enum, which have high integer values. To do that, we test whether the input key is in the range of a char by making sure it is less than 128.
      if (buflen == bufsize - 1) {    // If buflen has reached the maximum capacity we allocated (stored in bufsize) 
        bufsize *= 2;                 // then we double bufsize
        buf = realloc(buf, bufsize);  // and allocate that amount of memory before appending to buf
      }
      buf[buflen++] = c;  // add the new character just entered to buf
      buf[buflen] = '\0';  // make sure it ends with a null character
    }

    if (callback) callback(buf, c);  // the if (callback) allows the caller to pass NULL for the callback, in case they don't want to use the callback
  }
}

// -----------------------------------------------------------------------------
void editorMoveCursor(int key) {
  textRow *row = (Text.cursorYCoord >= Text.totalRows) ? NULL : &Text.row[Text.cursorYCoord];

  switch (key) {
    case ARROW_LEFT:
      if (Text.cursorXCoord != 0) {
        Text.cursorXCoord--;
      } else if (Text.cursorYCoord > 0) {
        Text.cursorYCoord--;
        Text.cursorXCoord = Text.row[Text.cursorYCoord].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && Text.cursorXCoord < row->size) {
        Text.cursorXCoord++;
      } else if (row && Text.cursorXCoord == row->size) {
        Text.cursorYCoord++;
        Text.cursorXCoord = 0;
      }
      break;
    case ARROW_UP:
      if (Text.cursorYCoord != 0) {
        Text.cursorYCoord--;
      }
      break;
    case ARROW_DOWN:
      if (Text.cursorYCoord != Text.totalRows) {
        Text.cursorYCoord++;
      }
      break;
  }

  row = (Text.cursorYCoord >= Text.totalRows) ? NULL : &Text.row[Text.cursorYCoord];
  int rowlen = row ? row->size : 0;
  if (Text.cursorXCoord > rowlen) {
    Text.cursorXCoord = rowlen;
  }
}

// -----------------------------------------------------------------------------
// waits for a keypress and handles it
void editorProcessKeypress() {
  static int quit_times = QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
    case '\r':  // Enter key
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):  // exit program if ctrl-q is pressed
      if (Text.modified && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);  // clear the screen
      write(STDOUT_FILENO, "\x1b[H", 3);  // position the cursor at the top left of the screen
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      Text.cursorXCoord = 0;
      break;

    case END_KEY:
      if (Text.cursorYCoord < Text.totalRows)
        Text.cursorXCoord = Text.row[Text.cursorYCoord].size;
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case BACKSPACE:      // mapped to 127
    case CTRL_KEY('h'):  // sends the control code 8, which is originally what the Backspace character would send back in the day
    case DELETE_KEY:        // mapped to <esc>[3~ (as seen in chapter 3)
      if (c == DELETE_KEY) editorMoveCursor(ARROW_RIGHT);  // pressing the right arrow and then backspace is equivalent to pressing the delete key
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          Text.cursorYCoord = Text.rowOffset;
        } else if (c == PAGE_DOWN) {
          Text.cursorYCoord = Text.rowOffset + Text.screenRows - 1;
          if (Text.cursorYCoord > Text.totalRows) Text.cursorYCoord = Text.totalRows;
        }

        int times = Text.screenRows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'):  // ctrl-L is traditionally used to refresh the screen in terminal programs (we do nothing because the screen is refreshed with every keypress by default)
    case '\x1b':  // escape - we ignore escape because there are many escape sequences we aren't handling, such as F1-F12
      break;

    default:
      editorInsertChar(c);  // 7-23-2020:5:23pm - step 103 - We’ve now officially upgraded our text viewer to a text editor
      break;
  }  // END: switch (c)

  quit_times = QUIT_TIMES;
}

/*** init ***/
// 8888888 888b    888 8888888 88888888888 
//   888   8888b   888   888       888     
//   888   88888b  888   888       888     
//   888   888Y88b 888   888       888     
//   888   888 Y88b888   888       888     
//   888   888  Y88888   888       888     
//   888   888   Y8888   888       888     
// 8888888 888    Y888 8888888     888     

// -----------------------------------------------------------------------------
// Initialize all the fields in the E struct
void initEditor() {
  Text.cursorXCoord = 0;
  Text.cursorYCoord = 0;
  Text.renderXCoord = 0;
  Text.rowOffset = 0;  // we'll be scrolled to the top of the file by default
  Text.columnOffset = 0;  // we'll be scrolled to the left of the file by default
  Text.totalRows = 0;
  Text.row = NULL;
  Text.modified = FALSE;
  Text.filename = NULL;
  Text.statusMsg[0] = '\0';
  Text.statusMsg_time = 0;

  if (getWindowSize(&Text.screenRows, &Text.screenColumns) == -1) die("getWindowSize");
  Text.screenRows -= 2;
}

// -----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
  
  for(;;)
  {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
