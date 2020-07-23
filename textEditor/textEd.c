// Small command line text editor that will run in a Linux terminal. 

// Small changes

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>      // needed for iscntrl()
#include <errno.h>      // needed for errno, EAGAIN
#include <stdio.h>      // needed for perror(), printf(), sscanf(), snprintf(), FILE, fopen(), getline(), vsnprintf()
#include <stdarg.h>     // needed for va_list, va_start(), and va_end()
#include <stdlib.h>     // Needed for exit(), atexit(), realloc(), free(), malloc()
#include <string.h>     // Needed for memcpy(), strlen(), strup(). memmove()
#include <sys/ioctl.h>  // Needed for struct winsize, ioctl(), TIOCGWINSZ 
#include <sys/types.h>  // Needed for ssize_t
#include <termios.h>    // Needed for struct termios, tcsetattr(), TCSAFLUSH, tcgetattr(), BRKINT, ICRNL, INPCK, ISTRIP, 
                        // IXON, OPOST, CS8, ECHO, ICANON, IEXTEN, ISIG, VMIN, VTIME
#include <time.h>       // Needed for time_t, time()
#include <unistd.h>     // Needed for read(), STDIN_FILENO, write(), STDOUT_FILENO

/*** defines ***/


#define TEXT_ED_VERSION    "0.0.1"
#define TAB_STOP           8

#define CTRL_KEY(k)        ((k) & 0x1F)  // unset the upper 3 bits of k

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/

typedef struct erow {  // the typedef lets us refer to the type as "erow" instead of "struct erow"
  int size;  // the quantity of elements in the *chars array
  int rsize;  // the quantity of elements in the *render array
  char *chars;  // pointer to a dynamically allocated array that holds all the characters in a single row of text as read from a file
  char *render;  // pointer to a dynamically allocated array that holds all the characters in a single row of text as they are displayed on the screen
} erow;  // erow stands for "editor row" - it stores a line of text as a pointer to the dynamically-allocated character data and a length

struct editorConfig {
  int cx, cy;  // cx - horizontal index into the chars field of erow (cursor location???)
  int rx;  //horizontal index into the render field of erow - if there are no tab son the current line, rx will be the same as cx 
  int rowoff;  // row offset - keeps track of what row of the file the user is currently scrolled to
  int coloff;  // column offset - keeps track of what column of the file the user is currently scrolled to
  int screenrows;  // qty of rows on the screen - window size
  int screencols;  // qty of columns on the screen - window size
  int numrows;  // the number of rows (lines) of text being displayed/stored by the editor
  erow *row;  // Hold a single row of test, both as read from a file, and as displayed on the screen
  char *filename;  // Name of the file being edited
  char statusmsg[80];  // holds an 80 character message to the user displayed on the status bar.
  time_t statusmsg_time;  // timestamp for the status message - current status message will only display for five seconds or until the next key is pressed since the screen in refreshed only when a key is pressed.
  struct termios orig_termios;  // structure to hold the original state of the terminal when our program began, before we started altering its state
};

struct editorConfig E;

/*** terminal ***/

// -----------------------------------------------------------------------------
void die(const char *s)  // prints error message and exits program with error code 1
{
  write(STDOUT_FILENO, "\x1b[2J", 4);  // clear the screen
  write(STDOUT_FILENO, "\x1b[H", 3);  // position the cursor at the top left of the screen

  perror(s);  // looks at the global errno variable and prints a secriptive error message for it preceeded by the string given to it
  exit(1);
}

// -----------------------------------------------------------------------------
void disableRawMode()  // disable raw text input mode
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {  // set the terminal's attribute flags to their original states
    die("tcsetattr");                                             // and exit program if there is an error
  }
}

// -----------------------------------------------------------------------------
void enableRawMode()  // enable raw text input mode
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {  // read the terminal's attribute flags into orig_terminos
    die("tcgetattr");                                  // and exit program if there is an error
  }
  atexit(disableRawMode);  // call the disableRawMode function automatically upon exit from the program form either retunr from main() 
                           // or using exit()

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);  // unset the input bitflags for enabling Ctrl-C, enabling Ctrl-M, enabling 
                                                             // parity checking, stripping the 8th bit of input, enabling Ctrl-S and Ctrl-Q
  raw.c_oflag &= ~(OPOST);  // unset the output flag for automatically prefixing a new line character with a carriage retrun character
  raw.c_cflag |= (CS8);  // set all the bits for character size to 8 bits per byte
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);  // unset the loacal bitflags for echoing to the terminal, canonical mode, enabling 
                                                    // Ctrl-V, enabling Ctrl-C and Ctrl-Z
  raw.c_cc[VMIN] = 0;  // sets the minimum number of bytes of input needed for read() to return to 0 - read can return as soon as there 
                       // is any input
  raw.c_cc[VTIME] = 1;  // sets the maximum amount of time to wait befoore read() returns to 1/10 of a second (100 milliseconds) - if 
                        // read() times out, it returns 0, which makes sense because its usual return value in the number of bytes read
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1 ) {  // set the terminal's attribute flags to those in raw
    die("tcsetattr");                                     // and exit program if there is an error
  }
}

// -----------------------------------------------------------------------------
// waits for one keypress and returns it
int editorReadKey() 
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {  // read 1 byte from standard input (keyboard) into c
    if (nread == -1 && errno != EAGAIN) die("read");  // and exit program if there is an error
  }

  if (c == '\x1b') {  // if c is an escape charater
    char seq[3];

    // read the next 2 bytes into seq
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
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
    return c;
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

/*** row operations ***/

// -----------------------------------------------------------------------------
// converts a chars index into a render index
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

// -----------------------------------------------------------------------------
// uses the chars string of an erow to fill in the contents of the render string - copy each character from chars to render
void editorUpdateRow(erow *row) {
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
}

// -----------------------------------------------------------------------------
// allocates memory space for a new erow at the end of the E.row array, then copies the given string to it 
void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
}

// -----------------------------------------------------------------------------
// erow *row - pointer to an erow struct
// int at - the index at which we want to insert the character (index to insert 'at'???)
// int c - new character to insert
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;  // validate at - Notice that at is allowed to go one character past the end of the string, in which case the character should be inserted at the end of the string.
  row->chars = realloc(row->chars, row->size + 2);  // Then we allocate one more byte for the chars of the erow (we add 2 because we also have to make room for the null byte)
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);  // use memmove() to make room for the new character. move everything from index at to the end of the string over one character leaving a "hole" at index at.  memmove() comes from <string.h>. It is like memcpy(), but is safe to use when the source and destination arrays overlap.
  row->size++;  // increment the size of the chars array, 
  row->chars[at] = c;  // assign the character to its position in the array
  editorUpdateRow(row);  // call editorUpdateRow() so that the render and rsize fields get updated with the new row content.
}

/*** editor operations ***/  // This section will contain functions that we’ll call from editorProcessKeypress() when we’re mapping keypresses to various text editing operations


// -----------------------------------------------------------------------------
// take a character and use editorRowInsertChar() to insert that character into the position that the cursor is at.
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {    // if the cursor is located on the very last line of the editor text (the cursor is on the tilde line after the end of the file, so we need to append a new row)
    editorAppendRow("", 0);  // allocate memory space for a new row - the new character will be inserted on a new row 
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);  // insert the character (c) into the specific row of the row array
  E.cx++;  // move the cursor forward so that the next character the user inserts will go after the character just inserted
}

/*** file i/o ***/

// -----------------------------------------------------------------------------
// for opening and reading a file from disk
void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || 
                           line[linelen - 1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

/*** append buffer ***/

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

// -----------------------------------------------------------------------------
//  check if the cursor has moved outside of the visible window, and if so, adjust E.rowoff so that the cursor is just inside the visible window.
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

// -----------------------------------------------------------------------------
// draws a tilde on every row of the terminal just like Vim
void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff; // To get the row of the file that we want to display at each y position, we add E.rowoff to the y position.
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Text Editor -- version %s", TEXT_ED_VERSION);
        if (welcomelen > E.screencols) {
          welcomelen = E.screencols;
        }
        int padding = (E.screencols - welcomelen) / 2;
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
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
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
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
    E.filename ? E.filename : "[No Name]", E.numrows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
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
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;  // if message length is grater than the width of the screen, cut it down
  if (msglen && time(NULL) - E.statusmsg_time < 5)  // display the message only if it is lenn than 5 seconds old
    abAppend(ab, E.statusmsg, msglen);
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
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (E.rx - E.coloff) + 1);
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
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/

// -----------------------------------------------------------------------------
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy != E.numrows) {
        E.cy++;
      }
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

// -----------------------------------------------------------------------------
// waits for a keypress and handles it
void editorProcessKeypress()
{
  int c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):  // exit program if ctrl-q is pressed
      write(STDOUT_FILENO, "\x1b[2J", 4);  // clear the screen
      write(STDOUT_FILENO, "\x1b[H", 3);  // position the cursor at the top left of the screen
      exit(0);
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }

        int times = E.screenrows;
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

    default:
      editorInsertChar(c);  // 7-23-2020:5:23pm - step 103 - We’ve now officially upgraded our text viewer to a text editor
      break;
  }
}

/*** init ***/

// -----------------------------------------------------------------------------
// Initialize all the fields in the E struct
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;  // we'll be scrolled to the top of the file by default
  E.coloff = 0;  // we'll be scrolled to the left of the file by default
  E.numrows = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

// -----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-Q = quit");
  
  while (1) 
  {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}