/* Remote target glue for the Hitachi SH-3 ROM monitor.
   Copyright 1995 Free Software Foundation, Inc.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "gdbcore.h"
#include "target.h"
#include "monitor.h"
#include "serial.h"
#include "srec.h"

static void sh3_open PARAMS ((char *args, int from_tty));

static void
sh3_supply_register (regname, regnamelen, val, vallen)
     char *regname;
     int regnamelen;
     char *val;
     int vallen;
{
  int numregs;
  int regno;

  numregs = 1;
  regno = -1;

  if (regnamelen == 2)
    switch (regname[0])
      {
      case 'S':
	if (regname[1] == 'R')
	  regno = SR_REGNUM;
	break;
      case 'P':
	if (regname[1] == 'C')
	  regno = PC_REGNUM;
	else if (regname[1] == 'R')
	  regno = PR_REGNUM;
	break;
      }
  else if (regnamelen == 3)
    switch (regname[0])
      {
      case 'G':
      case 'V':
	if (regname[1] == 'B' && regname[2] == 'R')
	  if (regname[0] == 'G')
	    regno = VBR_REGNUM;
	  else
	    regno = GBR_REGNUM;
	break;
#if 0
      case 'S':
	if (regname[1] == 'S' && regname[2] == 'R')
	  regno = SSR_REGNUM;
	else if (regname[1] == 'P' && regname[2] == 'C')
	  regno = SPC_REGNUM;
	break;
#endif
      }
  else if (regnamelen == 4)
    switch (regname[0])
      {
      case 'M':
	if (regname[1] == 'A' && regname[2] == 'C')
	  if (regname[3] == 'H')
	    regno = MACH_REGNUM;
	  else if (regname[3] == 'L')
	    regno = MACL_REGNUM;
	break;
      case 'R':
	if (regname[1] == '0' && regname[2] == '-' && regname[3] == '7')
	  {
	    regno = R0_REGNUM;
	    numregs = 8;
	  }
      }
  else if (regnamelen == 5)
    if (regname[1] == '8' && regname[2] == '-' && regname[3] == '1'
	&& regname[4] =='5')
      {
	regno = R0_REGNUM + 8;
	numregs = 8;
      }

  if (regno >= 0)
    while (numregs-- > 0)
      val = monitor_supply_register (regno++, val);
}

static void
sh3_load (desc, file, hashmark)
     serial_t desc;
     char *file;
     int hashmark;
{
  monitor_printf ("il;s:x\r");
  monitor_expect ("\005", NULL, 0); /* Look for ENQ */
  SERIAL_WRITE (desc, "\006", 1); /* Send ACK */
  monitor_expect ("LO x\r", NULL, 0); /* Look for filename */

  load_srec (desc, file, 80, SREC_ALL, hashmark);

  monitor_expect ("\005", NULL, 0); /* Look for ENQ */
  SERIAL_WRITE (desc, "\006", 1); /* Send ACK */
  monitor_expect_prompt (NULL, 0);
}

/* This array of registers need to match the indexes used by GDB.
   This exists because the various ROM monitors use different strings
   than does GDB, and don't necessarily support all the registers
   either. So, typing "info reg sp" becomes a "r30".  */

static char *sh3_regnames[NUM_REGS] = {
  "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
  "R8", "R9", "R10","R11","R12","R13","R14","R15",
  "PC", "PR", "GBR","VBR","MACH","MACL","SR",NULL,
  NULL,  NULL, NULL, NULL};

/* Define the monitor command strings. Since these are passed directly
   through to a printf style function, we may include formatting
   strings. We also need a CR or LF on the end.  */

static struct target_ops sh3_ops;

static char *sh3_inits[] = {"\003", NULL}; /* Exits sub-command mode & download cmds */

static struct monitor_ops sh3_cmds =
{
  MO_CLR_BREAK_USES_ADDR
    | MO_GETMEM_READ_SINGLE,	/* flags */
  sh3_inits,			/* monitor init string */
  "g\r",			/* continue command */
  "s\r",			/* single step */
  "\003",			/* Interrupt program */
  "b %x\r",			/* set a breakpoint */
  "b -%x\r",			/* clear a breakpoint */
  "b -\r",			/* clear all breakpoints */
  "f %x @%x %x\r",		/* fill (start len val) */
  {
    "m %x %x\r",		/* setmem.cmdb (addr, value) */
    "m %x %x;w\r",		/* setmem.cmdw (addr, value) */
    "m %x %x;l\r",		/* setmem.cmdl (addr, value) */
    NULL,			/* setmem.cmdll (addr, value) */
    NULL,			/* setreg.resp_delim */
    NULL,			/* setreg.term */
    NULL,			/* setreg.term_cmd */
  },
  {
    "m %x\r",			/* getmem.cmdb (addr, len) */
    "m %x;w\r",			/* getmem.cmdw (addr, len) */
    "m %x;l\r",			/* getmem.cmdl (addr, len) */
    NULL,			/* getmem.cmdll (addr, len) */
    "^ [0-9A-F]+ ",		/* getmem.resp_delim */
    "? ",			/* getmem.term */
    ".\r",			/* getmem.term_cmd */
  },
  {
    ".%s %x\r",			/* setreg.cmd (name, value) */
    NULL,			/* setreg.resp_delim */
    NULL,			/* setreg.term */
    NULL			/* setreg.term_cmd */
  },
  {
    ".%s\r",			/* getreg.cmd (name) */
    "=",			/* getreg.resp_delim */
    "? ",			/* getreg.term */
    ".\r"			/* getreg.term_cmd */
  },
  "r\r",			/* dump_registers */
				/* register_pattern */
  "\\(\\w+\\)=\\([0-9a-fA-F]+\\( +[0-9a-fA-F]+\\b\\)*\\)",
  sh3_supply_register,		/* supply_register */
  sh3_load,			/* load_routine */
  NULL,				/* download command */
  NULL,				/* Load response */
  "\n:",			/* monitor command prompt */
  "\r",				/* end-of-line terminator */
  ".\r",			/* optional command terminator */
  &sh3_ops,			/* target operations */
  SERIAL_1_STOPBITS,		/* number of stop bits */
  sh3_regnames,			/* registers names */
  MONITOR_OPS_MAGIC		/* magic */
};

static void
sh3_open (args, from_tty)
     char *args;
     int from_tty;
{
  monitor_open (args, &sh3_cmds, from_tty);
}

void
_initialize_sh3 ()
{
  init_monitor_ops (&sh3_ops);

  sh3_ops.to_shortname = "sh3";
  sh3_ops.to_longname = "Hitachi SH-3 rom monitor";
  sh3_ops.to_doc = "Debug on a Hitachi eval board running the SH-3 rom monitor.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  sh3_ops.to_open = sh3_open;

  add_target (&sh3_ops);
}
