// Small command line text editor that will run in a Linux terminal. 

// Small changes

/*** includes ***/

#include <ctype.h>      // needed for iscntrl()
#include <errno.h>      // needed for errno, EAGAIN
#include <stdio.h>      // needed for perror(), printf(), sscanf()
#include <stdlib.h>     // Needed for exit(), atexit(), realloc(), free()
#include <string.h>     // Needed for memcpy()
#include <sys/ioctl.h>  // Needed for struct winsize, ioctl(), TIOCGWINSZ 
#include <termios.h>    // Needed for struct termios, tcsetattr(), TCSAFLUSH, tcgetattr(), BRKINT, ICRNL, INPCK, ISTRIP, 
                        // IXON, OPOST, CS8, ECHO, ICANON, IEXTEN, ISIG, VMIN, VTIME
#include <unistd.h>     // Needed for read(), STDIN_FILENO, write(), STDOUT_FILENO

/*** defines ***/

#define CTRL_KEY(k)    ((k) & 0x1F)  // unset the upper 3 bits of k

/*** data ***/

struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
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
char editorReadKey() 
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {  // read 1 byte from standard input (keyboard) into c
    if (nread == -1 && errno != EAGAIN) {             // and exit program if there is an error
      die("read");
    }
  }
  return c;
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
      return -1;                                                  // cursor 999 spaces to the right, the second moves it 999 spaces down
    }
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT { NULL, 0}

// -----------------------------------------------------------------------------
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);  // allocate memeory for the new string

  if (new == NULL) {
    return;
  }
  memcpy(&new[ab->len], s, len);  // copy the contents of s onto the end of the contents of new
  ab->b = new;  // update ab.b
  ab->len += len;  // update ab.len
}

// -----------------------------------------------------------------------------
void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/

// -----------------------------------------------------------------------------
// draws a tilde on every row of the terminal just like Vim
void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenrows; y++) {
    abAppend(ab, "~", 1);

    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

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


/*** input ***/

// -----------------------------------------------------------------------------
// waits for a keypress and handles it
void editorProcessKeypress()
{
  char c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):  // exit program if ctrl-q is pressed
      write(STDOUT_FILENO, "\x1b[2J", 4);  // clear the screen
      write(STDOUT_FILENO, "\x1b[H", 3);  // position the cursor at the top left of the screen
      exit(0);
      break;
  }
}

/*** init ***/

// -----------------------------------------------------------------------------
// Initialize all the fields in the E struct
void initEditor()
{
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
    die("getWindowSize");
  }
}

// -----------------------------------------------------------------------------
int main()
{
  enableRawMode();
  initEditor();
  
  while (1) 
  {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}