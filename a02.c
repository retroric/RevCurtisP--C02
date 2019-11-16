/* Simple 6502 Assembler   *
 * for C02 Compiler        *
 * Uses DASM Syntax but    *
 * supports 65C02 Op Codes */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "a02.h"

#define DEBUG FALSE
int debug;  //Ouput Debug Info

enum otypes {BINFIL, PRGFIL}; //Object File Types

char objtyp;    //Object File Type
int  orgadr;    //Origin Address
int  curadr;    //Current Address
int  lstadr;    //List Address

struct sym {int block; char name[MAXLBL+1]; int bytes, value, refrd;};
struct sym symbol;          //Current Symbol
struct sym symtbl[MAXSYM];  //Global Symbol Table
int symcnt; //Number of Global Labels
int blknum; //Local Label Block Number (0 = Global)

char label[MAXSTR]; //Assembly Line Label
char mnmnc[MAXSTR]; //Opcode Mnemonic
char oprnd[MAXSTR]; //Opcode Mnemonic
char cmmnt[MAXSTR]; //Assembly Line Comment
char mcode[MAXSTR]; //Generated Bytes
char strng[MAXSTR]; //Parsed String

int  opridx;         //Index into Operand

unsigned char token, opmod;   //OpCode Token, Modifier
unsigned int  amode;          //Addressing Modes
int           zpage, opval;   //ZeroPage Flag, Operand Value

char hexadr[6];     //Current Address in Hexadecimal
char bytstr[5];     //String Representation of Byte
  
char inplin[MAXSTR]; //Input Buffer 
char *linptr;        //Pointer into Input Buffer
int  lineno;         //Input File Line Number
int  savlno;         //Line Number (Saved)

int  passno;         //Assembler Pass Number (1 or 2)
int  endasm;         //End Assembly Flag

char prgnam[256]; //Assembler Path and Name (from Command Line)
char inpnam[256]; //Input File Name
char outnam[256]; //Output File Name
char lstnam[256]; //List File Name
char incnam[256]; //Include File Name
FILE *inpfil;     //Input File Pointer
FILE *outfil;     //Output File Pointer
FILE *lstfil;     //List File Pointer
FILE *incfil;     //Include File Pointer

/* Print Usage Info and Exit */
void usage(char* appnam) {
  printf("Usage: %s [opts] asmfile objfile [lstfile]\n", appnam);
  printf(" Opts: -p - Commodore PRG format\n");
  printf("       -d - Output Debug Info\n");
  exit(EXIT_FAILURE);
}

/* Print Error Message and Exit */
void xerror(char* format, char *s) {
  if (lineno) fprintf(stderr, "%04d: ", lineno);
  fprintf(stderr, format, s);
  exit(EXIT_FAILURE);
}    

/* Open File with Error Checking */
FILE * opnfil(char* name, char* mode) {
  if (debug) printf("Opening file '%s' with mode '%s'\n", name, mode);
  FILE *fp = fopen(name, mode);
  if (!fp) xerror("Error Opening File '%s'\n", name);
  return fp;
}

/* Skip Character in Input Line *
 * Args: c - Character to Skip  *
 * Updates: linptr              */
int skpchr(char c) {
  if (*linptr == c) {linptr++; return TRUE;}
  else return FALSE;
}

/* Skip Spaces in Input Line *
 * Updates: linptr           */
void skpspc(void) {
  while (*linptr && *linptr <= ' ') linptr++;    
}

/* Parse Word from Input Line       *
 * Args: skip - Skip Spaces Flag    *
 *       *word - Buffer for Word    *
 * Updates: linptr                  *
 * Returns: Word Found (TRUE/FALSE) */
int pword(int skip, char* word) {
  int wrdlen = 0;
  if (skip) skpspc();
  while (isalnum(*linptr) || *linptr == '_') {
    word[wrdlen++] = toupper(*linptr);
    linptr++;
  }
  word[wrdlen] = 0; //Terminate String
  if (wrdlen) return TRUE; else return FALSE;
}

struct sym *fndsym(int block, char* name) {
  for (int i=0; i < symcnt; i++) {
    if (symtbl[i].block != block || strcmp(symtbl[i].name,name)) continue;
    return &symtbl[i];
  }
  return NULL;
}

/* Set Symbol Value and Size */
void setsym(int value, int bytes) {
  if (debug) printf("Setting Symbol %s to %d\n", symbol.name, value);
  symbol.value = value;
  if (bytes) symbol.bytes = bytes;
  else symbol.bytes = (value > 0xFF) ? 2 : 1;
  symbol.refrd = FALSE;
}

/* Add Character to Beginning of String */
void pfxstr(char c, char* s) {
    for (int i=strlen(s)+1; i; i--) 
      s[i] = s[i-1]; //Copy All Characters to the Right
    s[0] = c;        //Insert Character at Beginning
}

/* Parse Label from Input Line 
 * Sets: label 
 * Updates: linptr
 * Returns: Label Found (TRUE/FALSE) */
int plabel(void) {
  if (debug) puts("Parsing Label");
  int block = (skpchr('.')) ? blknum : 0; //Local Label Block Number
  int found = pword(FALSE, label); //Parse Word without Skipping Spaces
  if (debug) {
    if (found) printf("Found Label %s\n", label);
    else puts("No Label Found");
  }
  skpchr(':'); //Skip Optional Label Terminator
  if (found && passno == 1) {
    if (label[0] && fndsym(block, label)) xerror("Duplicate Label %s Encountered\n", label);
    if (debug) printf("Initializing Symbol %s\n", label);
    symbol.block = block;
    if (strlen(label) > MAXLBL) xerror("Label %s Too Long\n", label);
    strcpy(symbol.name, label);
    setsym(curadr, 0);
  }
  if (block) pfxstr('.', label);
  skpspc(); //Skip to Mnemonic, Comment, or EOL
  return found;
}

/* Copy Character to Operand and Increment */
int cpychr(int c) {
  if (c && toupper(*linptr) != c) return FALSE;
  if (opridx < MAXSTR) oprnd[opridx++] = toupper(*linptr);
  linptr++;
  return TRUE;
}

/* Evaluate Binary Number */
int evlbin() {
  int result = 0;
  cpychr('%');
  while (isdigit(*linptr)) {
    if (*linptr > '1') break;
    result = (result << 1) + *linptr - '0';
    cpychr(0);
  }
  return result;
}

/* Evaluate Binary Number */
int evlchr() {
  int result = 0;
  cpychr('\'');
  result = *linptr;
  cpychr(0);
  cpychr('\'');
  return result;
}

/* Evaluate Decimal Number */
int evldec() {
  int result = 0;
  while (isdigit(*linptr)) {
    result = result * 10 + *linptr - '0';
    cpychr(0);
  }
  return result;
}

/* Evaluate Hexadecimal Number */
int evlhex() {
  int result = 0;
  cpychr('$');
  while (isxdigit(*linptr)) {
    int digit = *linptr - '0';
    if (digit > 9) digit = digit - 7;
    result = (result << 4) + digit;
    cpychr(0);
  }
  return result;
}

/* Evaluate Symbol */
struct sym *evlsym() {
  char name[MAXSTR];
  int block = (cpychr('.')) ? blknum : 0;
  pword(TRUE, name);
  for (int i=0; name[i]; i++) if (opridx<MAXSTR) oprnd[opridx++] = name[i];
  struct sym *result = fndsym(block, name);
  if (passno == 2 && result == NULL) xerror("Undefined Symbol %s\n", name);
  if (result) result->refrd = TRUE; //Symbol was Referenced
  return result;
}

/* Evaluate Term in Operand */
int evltrm() {
  int result;
  skpspc();
  if (isalpha(*linptr) || *linptr == '.') {
    struct sym *target = evlsym();
    result = (target) ? target->value : 0x100;
  }
  else if (isdigit(*linptr)) 
    result = evldec();
  else switch(*linptr) {
    case '$': result = evlhex(); break;
    case '%': result = evlbin(); break;
    case '\'': result = evlchr(); break;
    default: result = -1;
  }
  skpspc();
  if (debug) printf("Term Evaluated to %d\n", result);
  return result;
}

/* Evaluate Operand */
int evlopd(int maxsiz) {
  int result = 0;
  int hilo = 0; //Return LSB (1) or MSB (2)
  int prns; //Optional Parenthesis after Hi/Low Operator
  if (debug) puts("Evaluating Operand");
  skpspc();
  if (cpychr('<')) hilo = 1;
  else if (cpychr('>')) hilo = 2;
  if (hilo) prns = cpychr('(');
  result = evltrm();
  if (result >= 0) 
    while (cpychr('+')) {
      int opdval = evltrm();
      if (opdval < 0) break;
      result += opdval;
    }
  if (hilo) {
    if (result < 0) xerror("Hi/Low Operator Requires Operand", "");
    if (prns) cpychr(')'); //
    switch (hilo) {
      case 1: result = result & 0xFF; break; //LSB
      case 2: result = result >> 8; break;   //MSB
    }
  }
  if (debug) printf("Operand Evaluated to %d\n", result);
  if (result > maxsiz) xerror("Operand Value too Large\n", "");
  return result;
}

/* Write Byte to Output File */
void outbyt(int b) {
  if (curadr > -1) curadr++;
  if (passno != 2) return;
  fputc(b & 0xFF, outfil);
  sprintf(bytstr, "%02X ", b);
  if (strlen(mcode) < 9) strcat(mcode, bytstr);
}

/* Write Word to Output File */
void outwrd(int w) {
  outbyt(w & 0xff);  
  outbyt(w >> 8);  
}

/* Lookup Opcode */
int lkpopc(struct opc opl[]) {
  if (debug) printf("Looking up Mnemonic %s\n", mnmnc);
  token = 0xFF; //Set Token to Invalid
  char mne[5]; strncpy(mne, mnmnc, 4); mne[4] = 0; //Truncate Mnemonic to Four Characters
  for (int i=0; opl[i].name[0]; i++) {
    if (strcmp(opl[i].name, mne)) continue;
    token = opl[i].token;
    amode = opl[i].amode;
    if (debug) printf("Found token %02X, amode %04X\n", token, amode);
    return TRUE;
  }
  return FALSE;
}

/* Assemble BYTE Pseudo-Op */
void asmbyt(void) {
  if (debug) puts("Assembling BYTE Pseudo-Op");
  do {
    if (cpychr('"')) { //String Operand
      while (!cpychr('"')) {outbyt(*linptr); cpychr(0); }
      skpspc();
    } else
      outbyt(evlopd(0xFF)); //Evaluate Operand
  } while (cpychr(','));
}

/* Assemble HEX Pseudo-Op */
void asmhex(void) {
  if (debug) puts("Assembling HEX Pseudo-Op");
  do {outbyt(evlhex(0xFF)); } while (cpychr(','));
}

/* Assemble WORD Pseudo-Op */
void asmwrd(void) {
  do {
    outwrd(evlopd(0xFFFF)); //Evaluate Operand
  } while (cpychr(','));
}

/* Assemble FILL Pseudo-Op */
void asmaln(void) {
  if (debug) puts("Assembling ALIGN Pseudo-Op");
  int size = evlopd(0xFFFF); if (size < 2) return;
  if (debug) printf("Aligning to %d Bytes\n", size);
  int fill = size - (curadr % size); if (fill == size) return;
  if (debug) printf("Filling %d Bytes\n", fill);
  for (int i=0; i<fill; i++) outbyt(0);
}

/* Assemble FILL Pseudo-Op */
void asmfll(void) {
  if (debug) puts("Assembling FILL Pseudo-Op");
  int size = evlopd(0xFFFF);
  if (debug) printf("Filling %d Bytes\n", size);
  for (int i=0; i<size; i++) outbyt(0);
}

/* Assemble EQU Pseudo-Op */
void asmequ(void) {
  if (label[0] == 0) xerror("EQUate without Label", 0);
  setsym(evlopd(0xFFFF), 0);
}

/* Assemble ORG Pseudo-Op */
void asmorg(void) {
  orgadr = evlopd(0xFFFF);
  if (passno == 1 && symbol.name[0]) {
    symbol.value = orgadr;
    symbol.bytes = 2;
  }
  if (passno == 2 && objtyp == PRGFIL) 
    outwrd(orgadr);
  curadr = orgadr;
  lstadr = orgadr;
}

/* Assemble PROCESSOR Pseudo-Op */
void asmprc(void) {
  skpspc();
  while (isalnum(*linptr)) cpychr(0);
  if (debug) printf("Ignoring Operand %s\n", oprnd);
}

/* Assemble SUBROUTINE Pseudo-Op */
void asmsub(void) {
  blknum++;
  sprintf(oprnd, "%d", blknum); opridx = strlen(oprnd);
  if (debug) printf("Block Number set to %s\n", oprnd);
}

/* Assemble INCLUDE Pseudo-Op */
void asminf(void) {
  int incidx = 0;
  if (debug) puts("Assembling INCLUDE Pseudo-Op");
  if (incfil) xerror("Nested INCLUDE not Allowed", "");
  if (!cpychr('"')) xerror("File Name Must be Quoted", ""); 
  while (*linptr && !cpychr('"')) {
    char c = *linptr; if (c == '/') c = '\\'; //Reverse Slashes for DOS/Windows
    incnam[incidx++] = c;
    cpychr(0);
  }
  incnam[incidx] = 0; //Terminate Include Name
  if (incidx == 0) xerror("INCLUDE requires file name\n", "");
  if (debug) printf("Include File Set to Name to '%s'\n", incnam);
}

/* Assemble END Pseudo-Op */
void asmend(void) {
  endasm = TRUE;
}

/* Assemble Pseudo-Op */
int asmpso(int dot) {
  if (lkpopc(psolst) == FALSE && dot == FALSE) return FALSE; 
  skpspc();
  if (debug) printf("Assembling Pseudo-Op %s, Token '%c'\n", mnmnc, token);
  switch (token) {
    case '=': asmequ(); break;  //EQU
    case 'B': asmbyt(); break;  //BYTE
    case 'H': asmhex(); break;  //BYTE
    case 'W': asmwrd(); break;  //WORD
    case 'F': asmfll(); break;  //FILL
    case 'S': asmsub(); break;  //SUBRoutine
    case 'I': asminf(); break;  //INCLude
    case '*': asmorg(); break;  //ORG
    case 'P': asmprc(); break;  //PROCessor
    case 'E': asmend(); break;  //END
    case 'A': asmaln(); break;  //ALIGn
    default: xerror("Undefined Pseudo-Op %s\n", mnmnc);
  }
  if (dot) pfxstr('.', mnmnc);
  return TRUE;
}

/* Check for Valid Addressing Mode */
int chkmod(int mode) {
  char* s = NULL; //Pointer to Addressing Mode Description
  for (int i=0; amdesc[i].amode; i++)
    if (amdesc[i].amode == mode) {s = amdesc[i].desc; break;}
  if (debug) printf("Checking Addressing Mode %s, %04X against %04X\n", s, mode, amode);
  if (mode & amode) return TRUE;
  xerror("Invalid Addressing Mode %s", s);
}

/* Assemble Branch Opcode */
void asmbrn(void) {
  int offset = 0;
  if (debug) printf("Assembling Branch Opcode Token 0x%02X\n", token);
  zpage = TRUE;
  if (isalpha(*linptr) || *linptr =='.') {
    struct sym *target = evlsym();
    if (target) offset = (target->value - curadr - 2);
  }
  else if (cpychr('+')) offset = evlopd(0xFF); 
  else if (cpychr('-')) offset = -evlopd(0xFF); 
  else xerror("Illegal Branch Operand\n", "");
  if ((offset > 127 || offset < -128) && passno == 2) 
    xerror("Branch Out of Range\n", "");
  if (debug) printf("Branch Offset %d\n", offset);
  opval = offset & 0xFF;
}

/* Assemble Immediate Mode Instruction */
void asmimd(void) {
  if (debug) printf("Assembling Immediate Opcode Token 0x%02X\n", token);
  opval = evlopd(0xFF);
  zpage = TRUE;
  opmod = 0x08;  //Immediate
}

/* Assemble Indirect Mode Instruction */
void asmind(void) {
  if (debug) puts("Assembling Indirect Mode Instruction");
  zpage = TRUE; opval = evlopd(0xFFFF);
  if (cpychr(',') && cpychr('X') && chkmod(INDCX)) cpychr(')'); ////(Indirect,X) opmod=0
  else if (cpychr(')')) {
    if (cpychr(',') && cpychr('Y') && chkmod(INDCY)) opmod = 0x10;  //(Indirect),Y
    else if (chkmod(INDCT)) opmod = 0x11;                             //(Indirect)
    if (token == 0x4C) zpage = FALSE;  //JMP (Indirect Absolute)
  }
  else chkmod(0);                          //Illegal Addressing Mode
  if (zpage && opval > 0x00FF) xerror("Operand Value too Large\n", "");
}

/* Assemble Implied/Accumulator/Absolute/ZeroPage Mode Instruction */
void asmiaz(void) {
  opval = evlopd(0xFFFF);
  if (opval < 0) {
    if (amode != IMPLD) //Implied
      if (chkmod(ACMLT)) opmod = 0x08;  //Accumulator
    return;
  }
  if (debug) printf("Assembling Absolute/ZeroPage 0x%02X\n", token);
  zpage = (opval <= 0xff) ? TRUE : FALSE;
  if (zpage && chkmod(ZPAGE)) opmod =  0x04;  //ZeroPage
  else if (chkmod(ABSLT)) opmod = 0x0C; //Absolute
  if (cpychr(',')) {
    if (cpychr('X')) {
      if (zpage && chkmod(ZPAGX)) opmod = 0x14;  //ZeroPage,X
      else if (chkmod(ABSLX)) opmod = 0x1C;    //Absolute,X
    } else if (cpychr('Y')) {
      if (zpage && (token == 0x82 || token == 0xA2))
        opmod = 0x14;  //ZeroPage,Y
      else {zpage = FALSE; opmod = 0x18;} //Absolute,Y 
    } else chkmod(0);                      //Illegal Addressing Mode
  }
}

/* Fix Opcode (if needed) */
unsigned char fixopc(void) {
  if (debug) printf("Fixing OpCode $%02X+$%02X\n", token, opmod);
  for (int i=0; opfix[i].token; i++)
    if (opfix[i].token == token && opfix[i].opmod == opmod) 
      return opfix[i].opcode;
  return token + opmod;
}

/* Ouput Opcode debug Info */
void dbgopc(void) {
  if (debug) printf("token=$%02X, opmod=$%02X, Address Mode: ", token, opmod);
  switch (opmod) {
    case 0x00: if (amode == IMPLD) puts("Implied"); else puts("(Indirect,X)"); break;
    case 0x08: if (opval < 0) puts("Accumulator"); else puts("#Immediate"); break;
    case 0x10: puts("(Indirect),Y"); break;
    case 0x11: puts("(Indirect)"); break;
    case 0x04: puts("ZeroPage"); break;
    case 0x0C: puts("Absolute"); break;
    case 0x14: if ((token == 0x82 || token == 0xA2)) puts("ZeroPage,Y");
               else puts("ZeroPage,X"); break;
    case 0x1C: puts("Absolute,X"); break;
    case 0x18: puts("Absolute,Y"); break;
    default: puts("UNKOWN");
  }
  if (opval < 0) puts("No Operand");
  else {
    printf("Operand Value %d, ", opval);
    if (zpage) puts("Zero Page"); else puts("Absolute");
  }
}

/* Assemble Opcode */
int asmopc(int dot) {
  opmod = 0;
  if (asmpso(dot)) return TRUE; //Check For/Assemble Pseudo-Op
  if (lkpopc(opclst) == FALSE) xerror("Invalid Mnemonic %s\n", mnmnc);
  if (debug) printf("Assembling Opcode Token 0x%02X, ", token);
  if (debug) printf("Addressing Mode Mask 0x%04X\n", amode);
  skpspc();
  if (amode == RELTV) asmbrn(); //Branch (Relative) Instruction
  else if (cpychr('#')) asmimd();  //Assemble Implied Instruction
  else if (cpychr('(')) asmind(); //Assemble Indirect Instruction
  else asmiaz(); //Assemble Implied/Accumulator/Absolute/ZeroPage Instruction
  if (debug) dbgopc();
  int opcode = fixopc(); 
  if (debug) printf("Writing OpCode $%02X\n", opcode);
  outbyt(opcode);                         
  if (opval >= 0) {
    if (zpage) outbyt(opval); //Byte Operand
    else outwrd(opval);                              //Word Operand
  }
  return TRUE;
}

/* Parse Opcode Mnemonic from Input Line 
 * Sets: mnmnc 
 * Updates: linptr
 * Returns: Label Found (TRUE/FALSE) */
int pmnmnc(void) {
  if (debug) puts("Parsing Mnemonic");
  int dot = cpychr('.'); //Optional Dot before Pseudo-Op
  int found = pword(TRUE, mnmnc);
  opridx = 0; //Initialize Operand Index
  if (found) asmopc(dot);
  oprnd[opridx] = 0; //Terminate Operand String
  return found;
}

/* Parse Comment from Input Line
 * Sets: cmmnt 
 * Updates: linptr */
void pcmmnt(void) {
  skpspc();
  int i = 0;
  while (*linptr >= ' ') cmmnt[i++] = *linptr++;
  cmmnt[i] = 0; //Terminate Comment
  if (debug) {if (i) printf("Comment: %s\n", cmmnt); else puts("No Comment Found");}
}

/* Add Label to Symbol Table */
void addsym() {
  if (symbol.value<0) xerror("Origin Not Set", "");
  memcpy(&symtbl[symcnt++], &symbol, sizeof(symbol));
}


/* Open Include File */
void opninc(void) {
  if (debug) printf("Opening Include File %s\n", incnam);
  if (lstfil) fputs("\n", lstfil);
  incfil = opnfil(incnam, "r");
  savlno = lineno;
  lineno = 1;
}

/* Close Include File */
void clsinc(void) {
  if (debug) printf("Closing Include File %s\n", incnam);
  if (lstfil) fputs("\n", lstfil);
  fclose(incfil);
  incfil = NULL;
  incnam[0] = 0;
  lineno = savlno;
  endasm = FALSE; //Clear End Flag for Return to Maun File
}

/* Assemble Input File (Two Pass)        *
 * Args: pass - Assembly Pass (1 or 2)   *
 * Requires: inpfil - Input File Pointer *
 * Uses: inplin - Input Line Buffer      *
 *       lineno - Input File Line Number */
void asmfil(int pass) {
  endasm = FALSE;   //Reset End Assembly Flag
  passno = pass;    //Assembly Pass Number
  if (debug) printf("Assembling Pass %d\n", pass);
  lineno = 1;       //Initialize Input File Line Number
  blknum = 1;       //Initialize Local Block Number
  orgadr = -1;      //Origin Address Not Set
  curadr = orgadr;  //Set Current Address to Origin
  if (debug) printf("Rewinding  Input File\n");
  rewind(inpfil); //Start at Beginning of Input File
  while (TRUE) {
    if (incfil) linptr = fgets(inplin, MAXSTR, incfil);
    else linptr = fgets(inplin, MAXSTR, inpfil);
    if (endasm || linptr == NULL) {if (incfil) {clsinc(); continue;} else break;}
    if (debug) printf("%05d %04X: %s", lineno, curadr, inplin);
    lstadr = curadr;  //Set List Address
    mcode[0] = 0; //Clear Generated Macbine Code
    plabel(); //Parse Label
    pmnmnc(); //Parse Mnemonic
    pcmmnt(); //Parse Comment
    if (passno == 1 && label[0]) addsym(); //Add Label to Table
    if (passno == 2) {
      if (lstadr < 0) hexadr[0] = 0; else sprintf(hexadr, "%04X", lstadr);
      fprintf(lstfil, "%05d %-4s %-9s%-7s %-5s %-16s %s\n", lineno, hexadr, mcode, label, mnmnc, oprnd, cmmnt );
      fflush(lstfil); //Flush Output Buffer in case of Segmentation Fault
    }
    lineno++;
    if (incnam[0] && incfil == NULL) opninc(); //Open Include File 
  }
}

/* Print Symbol Table */
void prtsym(void) {
  fprintf(lstfil, "\n%s Symbol Table\nBlock Name     Size Value Rfd\n", "Global");
  for (int i=0; i<symcnt; i++) {
    int refrd = (symtbl[i].refrd) ? '*' : ' ';
    fprintf(lstfil, "%5d %-8s %4d %5d  %c \n", symtbl[i].block, symtbl[i].name, symtbl[i].bytes, symtbl[i].value, refrd);
  }
}

/* Parse Command Line Option */
int pcoptn(char *argval) {
  if (argval[0] != '-') return FALSE;
  char option = toupper(argval[1]);
  if (debug) printf(" Option '%c'\n", option);
  switch(option) {
    case 'D': debug = TRUE; break; //Enable debug Output
    case 'P': objtyp = PRGFIL; break; //Commodore PRG File
    default: xerror("Illegal Command Line Option %s\n", argval);
  }
  return TRUE;
}

/* Parse Command Line Arguments */
void pcargs(int argc, char *argv[]) {
  int argnum = 0;
  for (int arg = 0; arg<argc; arg++) {
    if (debug) printf("Arg %d='%s'\n", arg, argv[arg]);
    if (arg == 0) {strcpy(prgnam, argv[arg]); continue;}
    if (pcoptn(argv[arg])) continue;
    switch (argnum++) {
       case 0: strcpy(inpnam, argv[arg]); break;
       case 1: strcpy(outnam, argv[arg]); break;
       case 2: strcpy(lstnam, argv[arg]); break;
       default: xerror("Too Many Arguments\n", "");
    }                
  }
  if (argnum<2) usage(argv[0]);
}

int main(int argc, char *argv[]) {
  debug = DEBUG;                    //Initialize Debug Flag
  lstnam[0] = 0; lstfil = NULL;     //Default to No List File 
  incnam[0] = 0; incfil = NULL;     //Include File Not Opened
  lineno = 0;                       //No Line Number (yet)
  objtyp = BINFIL;                  //Default Object File Type to Binary
  pcargs(argc, argv);               //Parse Command Line Arguments
  inpfil = opnfil(inpnam, "r");     //Open Input File
  outfil = opnfil(outnam, "wb");    //Open Output File
  if (lstnam[0])                    //If List File Name Specified
    lstfil = opnfil(lstnam, "w");   //  Open List File
  asmfil(1);                        //Assemble Input File (First Pass)
  asmfil(2);                        //Assemble Input File (First Pass)
  if (lstfil && symcnt) prtsym();   //Print Symbol Table
  exit(0);                          //Exit with No Errors
}

