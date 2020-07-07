// Small command line text editor that will run in a Linux terminal. 

/*** includes ***/

#include <ctype.h>    // needed for iscntrl()
#include <errno.h>    // needed for errno, EAGAIN
#include <stdio.h>    // needed for perror(), printf()
#include <stdlib.h>   // Needed for exit(), atexit()
#include <termios.h>  // Needed for struct termios, tcsetattr(), TCSAFLUSH, tcgetattr(), BRKINT, ICRNL, INPCK, ISTRIP, 
                      // IXON, OPOST, CS8, ECHO, ICANON, IEXTEN, ISIG, VMIN, VTIME
#include <unistd.h>   // Needed for read() and STDIN_FILENO

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

// -----------------------------------------------------------------------------
void die(const char *s)  // prints error message and exits program with error code 1
{
  perror(s);  // looks at the global errno variable and prints a secriptive error message for it preceeded by the string given to it
  exit(1);
}

// -----------------------------------------------------------------------------
void disableRawMode()  // disable raw text input mode
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {  // set the terminal's attribute flags to their original states
    die("tcsetattr");                                             // and exit program if there is an error
  }
}

// -----------------------------------------------------------------------------
void enableRawMode()  // enable raw text input mode
{
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {  // read the terminal's attribute flags into orig_terminos
    die("tcgetattr");                                  // and exit program if there is an error
  }
  atexit(disableRawMode);  // call the disableRawMode function automatically upon exit from the program form either retunr from main() 
                           // or using exit()

  struct termios raw = orig_termios;
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

/*** init ***/

// -----------------------------------------------------------------------------
int main()
{
  enableRawMode();
  
  while (1) {
    char c = '\0';  // set c to null
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {  // read 1 byte from standard input (keyboard) into c
      die("read");                                             // and exit program if there is an error
    }
    if (iscntrl(c)) {  // if c is a control charater (ascii 0-31, 127) only 
      printf("%d\r\n", c);  // print ascii code only, not character, which is unprintable
    } else {
      printf("%d ('%c')\r\n", c, c);  // print ascii code and character
    }
    if (c == 'q') break;  // exit loop if 'q' is input
  }

  return 0;
}