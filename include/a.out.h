#ifndef _A_OUT_H
#define _A_OUT_H

#define __GNU_EXEC_MACROS__


// 第6--108行是该文件第1部分.定义目标文件执行结构以及相关操作的宏定义.
// ================================
// unsigned long a_magic;	// 执行文件魔数.使用N_MAGIC等宏访问.
// unsigned a_text;		// 代码长度,字节数.
// unsigned a_data;		// 数据长度,字节数.
// unsigned a_bss;		// 文件中的未初始化数据区长度,字节数.
// unsigned a_syms;		// 文件中的符号表长度,字节数.
// unsigned a_entry;		// 执行开始地址.
// unsigned a_trsize;		// 代码重定位信息长度,字节数.
// unsigned a_drsize;		// 数据重定位信息长度,字节数.
// --------------------------------
struct exec {
  unsigned long a_magic;	/* Use macros N_MAGIC, etc for access */
  unsigned a_text;		/* length of text, in bytes */
  unsigned a_data;		/* length of data, in bytes */
  unsigned a_bss;		/* length of uninitialized data area for file, in bytes */
  unsigned a_syms;		/* length of symbol table data in file, in bytes */
  unsigned a_entry;		/* start address */
  unsigned a_trsize;		/* length of relocation info for text, in bytes */
  unsigned a_drsize;		/* length of relocation info for data, in bytes */
};

// 用于取上述exec结构中的魔数.
#ifndef N_MAGIC
#define N_MAGIC(exec) ((exec).a_magic)
#endif

#ifndef OMAGIC
/* Code indicating object file or impure executable.  */
/* 指明为目标文件或者不纯的可执行文件的代号 */
// 历史上最早在PDP-11计算机上,魔数(幻数)是八进制数0407(0x107).它位于执行程序头结构开始处.原本是PDP-11的一条跳转指令,表示跳转到随后7个字后的代码开始处.
// 这样加载程序(loader)就可以在把执行文件放入内存后直接跳转到指令开始处运行.现在已没有程序使用这种方法,但这个八进制数却作为识别文件类型的标志(魔数)保留
// 了下来.
// OMAGIC可以认为是Old Magic的意思.
#define OMAGIC 0407
/* Code indicating pure executable.  */
/* 指明为纯可执行文件的代号 */	// New Magic,1975年以后开始使用.涉及虚存机制.
#define NMAGIC 0410		// 0410 == 0x108
/* Code indicating demand-paged executable.  */
/* 指明为需求分页处理的可执行文件 */	// 其头结构占用文件开始处1K空间.
#define ZMAGIC 0413		// 0413 == 0x10b
#endif /* not OMAGIC */
// 另外还有一个OMAGIC,是为节约磁盘容量,把盘上执行文件的头结构与代码紧凑存放.下面宏用于判断魔数字段的正确性.如果魔数不能被识别,则返回真.

#ifndef N_BADMAG
#define N_BADMAG(x)					\
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC		\
  && N_MAGIC(x) != ZMAGIC)
#endif

#define _N_BADMAG(x)					\
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC		\
  && N_MAGIC(x) != ZMAGIC)

// 目标文件头结构末端到1024字节之间的长度.
#define _N_HDROFF(x) (SEGMENT_SIZE - sizeof (struct exec))

// 下面宏用于操作目标文件的内容,包括.o模块文件和可执行文件.

// 代码部分起始偏移值.
// 如果文件是ZMAGIC类型的,即是执行文件,那么代码部分是从执行文件的1024字节偏移处开始.否则执行代码部分紧随执行头结构末端(32字节)开始,即文件是模块文件(OMAGIC类型).
#ifndef N_TXTOFF
#define N_TXTOFF(x) \
 (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) : sizeof (struct exec))
#endif

// 数据部分起始偏移值。从代码部分末端开始。
#ifndef N_DATOFF
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#endif

// 代码重定位信息偏移值。从数据部分末端开始。
#ifndef N_TRELOFF
#define N_TRELOFF(x) (N_DATOFF(x) + (x).a_data)
#endif

// 数据重定位信息偏移值。从代码重定位信息末端开始。
#ifndef N_DRELOFF
#define N_DRELOFF(x) (N_TRELOFF(x) + (x).a_trsize)
#endif

// 符号表偏移值。从上面数据段重定位表末端开始。
#ifndef N_SYMOFF
#define N_SYMOFF(x) (N_DRELOFF(x) + (x).a_drsize)
#endif

// 字符串信息偏移值。在符号表之后。
#ifndef N_STROFF
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)
#endif

// 下面对可执行文件被加载到内存（逻辑空间）中的位置情况进行操作。
/* Address of text segment in memory after it is loaded.  */
/* 代码段加载后在内存中的地址 */
#ifndef N_TXTADDR
#define N_TXTADDR(x) 0          // 可见，代码段从地址0开始执行。
#endif

/* Address of data segment in memory after it is loaded.
   Note that it is up to you to define SEGMENT_SIZE
   on machines not listed here.  */
/* 数据段加载后在内存中的地址.注意,对于下面没有列出名称的机器,需要你自己来定义对应的SEGMENT_SIZE */
#if defined(vax) || defined(hp300) || defined(pyr)
#define SEGMENT_SIZE PAGE_SIZE
#endif
#ifdef	hp300
#define	PAGE_SIZE	4096
#endif
#ifdef	sony
#define	SEGMENT_SIZE	0x2000
#endif	/* Sony.  */
#ifdef is68k
#define SEGMENT_SIZE 0x20000
#endif
#if defined(m68k) && defined(PORTAR)
#define PAGE_SIZE 0x400
#define SEGMENT_SIZE PAGE_SIZE
#endif

// 这里,Linux0.12内核把内存页定义为4KB,段大小定义为1KB.因此没有使用上面的定义.
#define PAGE_SIZE 4096
#define SEGMENT_SIZE 1024

// 以段为界的大小（进位方式）。
#define _N_SEGMENT_ROUND(x) (((x) + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1))

// 代码段尾地址。
#define _N_TXTENDADDR(x) (N_TXTADDR(x)+(x).a_text)

// 数据段开始地址。
// 如果文件是OMAGIC类型的，那么数据段就直接紧随代码段后面。否则的话数据段地址从代码段后面段边界开始（1KB边界对齐）
// 例如ZMAGIC类型文件。
#ifndef N_DATADDR
#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x)) \
     : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))
#endif

/* Address of bss segment in memory after it is loaded.  */
/* bss段加载到内存以后的地址 */
// 未初始化数据段bss位于数据段后面，紧跟数据段。
#ifndef N_BSSADDR
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)
#endif

// 对目标文件中的符号表项和相关操作宏进行定义和说明。a.out目标文件中符号表项结构（符号表记录结构）。
#ifndef N_NLIST_DECLARED
struct nlist {
  union {
    char *n_name;
    struct nlist *n_next;
    long n_strx;
  } n_un;                       // 该字节分成3个字段，146--154行是相应字段的屏蔽码。
  unsigned char n_type;
  char n_other;
  short n_desc;
  unsigned long n_value;
};
#endif

// 下面定义nlist结构中n_type字段值的常量符号。
#ifndef N_UNDF
#define N_UNDF 0
#endif
#ifndef N_ABS
#define N_ABS 2
#endif
#ifndef N_TEXT
#define N_TEXT 4
#endif
#ifndef N_DATA
#define N_DATA 6
#endif
#ifndef N_BSS
#define N_BSS 8
#endif
#ifndef N_COMM
#define N_COMM 18
#endif
#ifndef N_FN
#define N_FN 15
#endif

// 以下3个常量定义是nlist结构中n_type字段的屏蔽码（八进制表示）。
#ifndef N_EXT
#define N_EXT 1                 // 0x01（0b0000,0001）符号是否是外部的（全局的）。
#endif
#ifndef N_TYPE
#define N_TYPE 036              // 0x1e（0b0001,1110）符号的类型位。
#endif
#ifndef N_STAB                  // STAB -- 符号表类型（Symbol table types）。
#define N_STAB 0340             // 0xe0（0b1110,0000）这几个比特用于符号调试器。
#endif

/* The following type indicates the definition of a symbol as being
   an indirect reference to another symbol.  The other symbol
   appears as an undefined reference, immediately following this symbol.

   Indirection is asymmetrical.  The other symbol's value will be used
   to satisfy requests for the indirect symbol, but not vice versa.
   If the other symbol does not have a definition, libraries will
   be searched to find a definition.  */
/* 下面的类型指明对一个符号的定义是作为对另一个符号的间接引用。紧接该符号的其他的符号呈现为未定义的引用。
 * 
 * 这种间接引用是对称的。另一个符号的值将被用于满足间接符号的要求，但反之则不然。如果另一个符号没有定义，则将
 * 搜索库来寻找一个定义 */
#define N_INDR 0xa

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   element's value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references.  */
/* 下面的符号与集合元素有关。所有具有相同名称N_SET[ATDB]的符号形成一个集合。在代码部分中已为集合分配了空间，并且
 * 每个集合元素的值存放在一个字（word）的空间中。空间的第一个字存有集合的长度（集合元素数目）。
 * 
 * 集合的地址被放入一个N_SETV符号中，它的名称与集合同名。在满足未定义的外部引用方面，该符号的行为像一个N_DATA全局
 * 符号。 */

/* These appear as input to LD, in a .o file.  */
/* 以下这些符号在.o文件中是作为链接程序LD的输入 */
#define	N_SETA	0x14		/* Absolute set element symbol */       /* 绝对集合元素符号 */
#define	N_SETT	0x16		/* Text set element symbol */           /* 代码集合元素符号 */
#define	N_SETD	0x18		/* Data set element symbol */           /* 数伪集合元素符号 */
#define	N_SETB	0x1A		/* Bss set element symbol */            /* Bss集合元素符号 */

/* This is output from LD.  */
/* 下面是LD的输出。 */
#define N_SETV	0x1C		/* Pointer to set vector in data area.  */
                                /* 指向数据区中集合向量。 */

#ifndef N_RELOCATION_INFO_DECLARED

/* This structure describes a single relocation to be performed.
   The text-relocation section of the file is a vector of these structures,
   all of which apply to the text section.
   Likewise, the data-relocation section applies to the data section.  */
/* 下面结构描述单个重定位操作的执行。文件的代码重定位部分是这些结构的一个数组，所有这些适用于代码部分。类似地，数据重定位
   部分用于数据部分。 */

// a.out目标文件中代码和数据重定位信息结构。
struct relocation_info
{
  /* Address (within segment) to be relocated.  */
  /* 段内需要重定位的地址。 */
  int r_address;
  /* The meaning of r_symbolnum depends on r_extern.  */
  /* r_symbolnum的含义与r_extern有关。 */
  unsigned int r_symbolnum:24;
  /* Nonzero means value is a pc-relative offset
     and it should be relocated for changes in its own address
     as well as for changes in the symbol or section specified.  */
  /* 非零意味着值是一个pc相关的偏移值，因而在其自己地址空间以及符号或指定的节改变时，需要被重定位 */
  unsigned int r_pcrel:1;
  /* Length (as exponent of 2) of the field to be relocated.
     Thus, a value of 2 indicates 1<<2 bytes.  */
  /* 需要被重定位的字段长度（是2的次方）。因此，若值是2则表示1<<2字节数。 */
  unsigned int r_length:2;
  /* 1 => relocate with value of symbol.
          r_symbolnum is the index of the symbol
	  in file's the symbol table.
     0 => relocate with the address of a segment.
          r_symbolnum is N_TEXT, N_DATA, N_BSS or N_ABS
	  (the N_EXT bit may be set also, but signifies nothing).  */
  /* 1 => 以符号的值重定位。
          r_symbolnum是文件符号表中符号的索引。
     0 => 以段的地址进行重定位。
          r_symbolnum是N_TEXT、N_DATA、N_BSS或N_ABS
          （N_EXT比特位也可以被设置，但是毫无意义）。*/
  unsigned int r_extern:1;
  /* Four bits that aren't used, but when writing an object file
     it is desirable to clear them.  */
  /* 没有使用的4个比特位，但是当进程进行写一个目标文件时，最好将它们复位掉。 */
  unsigned int r_pad:4;
};
#endif /* no N_RELOCATION_INFO_DECLARED.  */


#endif /* __A_OUT_GNU_H__ */
