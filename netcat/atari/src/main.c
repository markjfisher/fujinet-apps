/**
 * A simple Netcat like program in CC65
 */

#include <atari.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h> // for kbhit() and cgetc()
#include "conio.h" // our local one.
#include "nio.h"

char url[256];				// URL
bool running = true;		// Is program running?
unsigned char trans = 0;	// Translation value (0,1,2,3)
char tmp[8];				// temporary # to string
unsigned char err;			// error code of last operation.
unsigned char trip = 0;		// if trip=1, fujinet is asking us for attention.
unsigned char ih_count = 0; // count the number of times IH is invoked
bool old_enabled = false;	// were interrupts enabled for old vector
void *old_vprced;			// old PROCEED vector, restored on exit.
unsigned short bw = 0;		// # of bytes waiting.
unsigned char rx_buf[8192]; // RX buffer.
unsigned char tx_buf[64];	// TX buffer.
bool echo = false;			// local echo?
unsigned char txbuflen;		// TX buffer length
unsigned char i;
char login[256];	// username for login
char password[256]; // password for login

extern void ih(); // defined in intr.s

/**
 * Get URL from user.
 */
bool get_url(int argc, char *argv[])
{

	OS.lmargn = 2;	// Set left margin to 2
	OS.shflok = 64; // turn on shift lock.

	if (!_is_cmdline_dos())
	{
		print("Version: " __DATE__ " " __TIME__ "\x9b");
		print("NETCAT <DEVICESPEC>\x9b");
		print("(e.g.: n1:tcp://foobar.com/)\x9b");
		get_line(url, 255);

		if (url[0] == '\x9b')
			strcpy(url, "n1:tcp://bbs.fozztexx.net/");

		print("Translation\x9B  0=NONE, 1=CR, 2=LF, *3=CR/LF?\x9b  *=Default\x9b");
		get_line(tmp, 7);
		if (tmp[0] == '\x9b')
			trans = 3;
		else
			trans = atoi(tmp);

		print("Local echo?--Y=YES, *N=NO? *=Default\x9b");
		get_line(tmp, 7);
		echo = (tmp[0] == 'Y' ? true : false);

		print("Username? (Return if none)\x9b");
		get_line(login, 128);

		print("Password? (Return for none)\x9b");
		get_line(password, 128);

		nlogin(url, login, password);
	}
	else
	{
		trans = echo = 0;

		strcpy(url, argv[1]);

		if (argc < 2)
		{
			print("nc <Nx:proto://host:port/path/> [/CR|LF|CRLF] [/E]\x9b\x9b");
			print("  /CR - Convert CR<->EOL\x9b");
			print("  /LF - Convert LF<->EOL\x9b");
			print("  /CRLF - Convert CR/LF<->EOL\x9b");
			return false;
		}
		else if (argc > 3)
			echo = ((argv[3][0] == '/') && (argv[3][1] == 'E'));
		else if ((argc > 2) || (argv[2][0] == '/') && (argv[2][1] == 'C') && (argv[2][2] == 'R') && (argv[2][3] == 'L') && (argv[2][4] == 'F'))
			trans = 3;
		else if ((argc > 2) || (argv[2][0] == '/') && (argv[2][1] == 'L') && (argv[2][2] == 'F'))
			trans = 2;
		else if ((argc > 2) || (argv[2][0] == '/') && (argv[2][1] == 'C') && (argv[2][2] == 'R'))
			trans = 1;
	}
	return true;
}

/**
 * Print error
 */
void print_error(unsigned char err)
{
	itoa(err, tmp, 10);
	print(tmp);
	print("\x9b");
}

/**
 * NetCat
 */
void nc()
{
	OS.lmargn = 0; // Set left margin to 0
	OS.shflok = 0; // turn off shift-lock.

	// Attempt open.
	print("Opening:\x9b");
	print(url);
	print("\x9b");

	err = nopen(url, trans);

	if (err != SUCCESS)
	{
		print("\x9bOPEN ERROR: ");
		print_error(err);
		nclose(url);
		return;
	}

	// Open successful, set up interrupt
	old_vprced = OS.vprced;		 // save the old interrupt vector
	old_enabled = PIA.pactl & 1; // keep track of old interrupt state
	PIA.pactl &= (~1);			 // Turn off interrupts before changing vector
	OS.vprced = ih;				 // Set PROCEED interrupt vector to our interrupt handler.
	trip = 0;
	ih_count = 0;
	PIA.pactl |= 1; // Indicate to PIA we are ready for PROCEED interrupt.

	// MAIN LOOP ///////////////////////////////////////////////////////////

	while (running)
	{
		// If key pressed, send it.
		while (kbhit())
		{
			tx_buf[txbuflen++] = cgetc();
		}

		if (txbuflen > 0)
		{
			if (echo == true)
				for (i = 0; i < txbuflen; i++)
					printc(&tx_buf[i]);

			err = nwrite(url, tx_buf, txbuflen); // Send character.

			if (err != 1)
			{
				print("\x9b" "WRITE ERROR: ");
				print_error(err);
				running = false;
				continue;
			}
			txbuflen = 0;
		}

		if (trip == 0) // is anything waiting for us?
			continue;

		// Yes, fetch status and decide if we need to read anything
		err = nstatus(url);

		if (err == 136)
		{
			print("\x9b" "DISCONNECTED.\x9b"); // we can't combine the two strings because the compiler thinks \x9bDI is hex, and errors
			running = false;
			trip = 0;
			break; // Exit the loop immediately to call nclose()
		}
		else if (err != 1)
		{
			print("\x9b" "STATUS ERROR: ");
			print_error(err);
			running = false;
			trip = 0;
			continue;
		}

		// Get # of bytes waiting, no more than size of rx_buf
		bw = OS.dvstat[1] * 256 + OS.dvstat[0];

		if (bw > sizeof(rx_buf))
			bw = sizeof(rx_buf);

		if (bw > 0)
		{
			err = nread(url, rx_buf, bw);

			if (err != 1)
			{
				print("\x9b" "READ ERROR: ");
				print_error(err);
				running = false;
				continue;
			}

			// Print the buffer to screen.
			printl(rx_buf, bw);

			trip = 0;
			PIA.pactl |= 1; // Flag interrupt as serviced, ready for next one.
		} // if bw > 0
	} // while running

	print("ih_count: ");
	print_error(ih_count);
	print("trip: ");
	print_error(trip);

	// END MAIN LOOP ///////////////////////////////////////////////////////

	// Close the connection
	nclose(url);

	// Restore old PROCEED interrupt.
	PIA.pactl &= ~1; // disable interrupts
	OS.vprced = old_vprced;
	PIA.pactl |= old_enabled;
}

/**
 * Main entrypoint
 */
int main(int argc, char *argv[])
{
	OS.soundr = 0; // Turn off SIO beeping sound
	cursor(1);	   // Keep cursor on

	while (running == true)
	{
		if (get_url(argc, argv))
			nc();
		else
			running = false;
	}

	OS.soundr = 3; // Restore SIO beeping sound
	return 0;
}
