/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"
#include "Lib/SafeString.h"

#if defined(_DEBUG) || defined(IG_DEBUG_STACKTRACE)

#pragma pack(push, 8)

#pragma comment(linker, "/defaultlib:Dbghelp.lib")

#include "Common/StackDump.h"
#include "Common/Debug.h"
#include "Common/CrashReport.h"


//*****************************************************************************
//	Prototypes
//*****************************************************************************
BOOL InitSymbolInfo(void);
void UninitSymbolInfo(void);
// Stack walker for x64. The context comes from RtlCaptureContext; addresses use the 64-bit
// variants of the dbghelp API (StackWalk64, SymGetSymFromAddr64, SymGetLineFromAddr64).
#define STACKDUMP_MACHINE	IMAGE_FILE_MACHINE_AMD64
#define CTX_IP(c)			((c).Rip)
#define CTX_SP(c)			((c).Rsp)
#define CTX_FP(c)			((c).Rbp)
static void InitStackFrame(STACKFRAME64& frame, const CONTEXT& ctx)
{
	memset(&frame, 0, sizeof(frame));
	frame.AddrPC.Mode = AddrModeFlat;		frame.AddrPC.Offset = CTX_IP(ctx);
	frame.AddrStack.Mode = AddrModeFlat;	frame.AddrStack.Offset = CTX_SP(ctx);
	frame.AddrFrame.Mode = AddrModeFlat;	frame.AddrFrame.Offset = CTX_FP(ctx);
}
static BOOL WalkOneFrame(STACKFRAME64& frame, CONTEXT& ctx)
{
	return ::StackWalk64(STACKDUMP_MACHINE, GetCurrentProcess(), GetCurrentThread(), &frame, &ctx,
		nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
}
void MakeStackTrace(const CONTEXT* context, int skipFrames, void (*callback)(const char*));
void GetFunctionDetails(void *pointer, char*name, size_t nameSize, char*filename, size_t filenameSize, unsigned int* linenumber, unsigned int* address);
void WriteStackLine(void*address, void (*callback)(const char*));

//*****************************************************************************
//	Mis-named globals :-)
//*****************************************************************************
static Bool gsInit=FALSE;

BOOL (__stdcall *gsSymGetLineFromAddr)(
		IN  HANDLE                  hProcess,
		IN  DWORD64                 dwAddr,
		OUT PDWORD                  pdwDisplacement,
		OUT PIMAGEHLP_LINE64        Line
			);	// 64-bit variant


//*****************************************************************************
//*****************************************************************************
void StackDumpDefaultHandler(const char*line)
{
	DEBUG_LOG(("%s", line));
}


//*****************************************************************************
//*****************************************************************************
void StackDump(void (*callback)(const char*))
{
	if (callback == nullptr) 
	{
		callback = StackDumpDefaultHandler;
	}

	InitSymbolInfo();

	CONTEXT ctx;
	::RtlCaptureContext(&ctx);
	MakeStackTrace(&ctx, 2, callback);
}


//*****************************************************************************
//*****************************************************************************
void StackDumpFromContext(const CONTEXT* context, void (*callback)(const char*))
{
	if (callback == nullptr) 
	{
		callback = StackDumpDefaultHandler;
	}
	InitSymbolInfo();
	MakeStackTrace(context, 0,  callback);
}


//*****************************************************************************
//*****************************************************************************
BOOL InitSymbolInfo()
{
	if (gsInit == TRUE) 
		return TRUE;

	gsInit = TRUE;

	atexit(UninitSymbolInfo);

	// See if we have the line from address function
	// We use GetProcAddress to stop link failures at dll loadup
	HINSTANCE hInstDebugHlp = GetModuleHandle("dbghelp.dll");

	gsSymGetLineFromAddr = (BOOL (__stdcall *)(	IN  HANDLE,IN  DWORD64,OUT PDWORD,OUT PIMAGEHLP_LINE64))
							GetProcAddress(hInstDebugHlp , "SymGetLineFromAddr64");

	char pathname[_MAX_PATH+1];
	char drive[10];
	char directory[_MAX_PATH+1];
	HANDLE process;


	::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_OMAP_FIND_NEAREST);

	process = GetCurrentProcess();

	//Get the apps name
	::GetModuleFileName(nullptr, pathname, _MAX_PATH);

	// turn it into a search path
	_splitpath(pathname, drive, directory, nullptr, nullptr);
	WWFormat::Into(pathname, "{}:\\{}", drive, directory);

	// append the current directory to build a search path for SymInit
	safe_strcat(pathname, ";.;");

	// fInvadeProcess = TRUE loads the symbols of all loaded modules (exe + DLLs); a manual SymLoadModule64
	// with base address 0 yields no resolvable symbols on x64 ("<Unknown>" in the log).
	if(::SymInitialize(process, pathname, TRUE))
	{
		return TRUE;
	}

	return(FALSE);
}


//*****************************************************************************
//*****************************************************************************
void UninitSymbolInfo(void)
{
	if (gsInit == FALSE)
	{
		return;
	}

	gsInit = FALSE;

	::SymCleanup(GetCurrentProcess());
}



//*****************************************************************************
//*****************************************************************************
void MakeStackTrace(const CONTEXT* context, int skipFrames, void (*callback)(const char*))
{
STACKFRAME64    stack_frame;
BOOL            b_ret = TRUE;
CONTEXT         walkCtx = *context;	// StackWalk64 modifies the context (required on x64)
InitStackFrame(stack_frame, walkCtx);
{
/*
    if(GetThreadContext(thread, &gsContext))
    {
        memset(&stack_frame, 0, sizeof(STACKFRAME));
        stack_frame.AddrPC.Mode = AddrModeFlat;
        stack_frame.AddrPC.Offset = gsContext.Eip;
        stack_frame.AddrStack.Mode = AddrModeFlat;
        stack_frame.AddrStack.Offset = gsContext.Esp;
        stack_frame.AddrFrame.Mode = AddrModeFlat;
        stack_frame.AddrFrame.Offset = gsContext.Ebp;
*/

		//{
			callback("Call Stack\n**********\n");

			// Skip some ?
			unsigned int skip = skipFrames;
			while (b_ret&&skip)
			{
					b_ret = WalkOneFrame(stack_frame, walkCtx);
					skip--;
			}

			skip = 30;
			while(b_ret&&skip)
			{

					b_ret = WalkOneFrame(stack_frame, walkCtx);
					

					
					if (b_ret) WriteStackLine(reinterpret_cast<void *>(static_cast<DWORD_PTR>(stack_frame.AddrPC.Offset)), callback);
					skip--;
			}
	}
}


//*****************************************************************************
//*****************************************************************************
void GetFunctionDetails(void *pointer, char*name, size_t nameSize, char*filename, size_t filenameSize, unsigned int* linenumber, unsigned int* address)
{
	InitSymbolInfo();
	if (name)
	{
		WWFormat::Into(name, nameSize, "{}", "<Unknown>");
	}
	if (filename)
	{
		WWFormat::Into(filename, filenameSize, "{}", "<Unknown>");
	}
	if (linenumber)
	{
		*linenumber = 0xFFFFFFFF;
	}
	if (address)
	{
		*address = 0xFFFFFFFF;
	}

	DWORD64 displacement = 0;
    HANDLE process = ::GetCurrentProcess();
    char symbol_buffer[512 + sizeof(IMAGEHLP_SYMBOL64)];
    memset(symbol_buffer, 0, sizeof(symbol_buffer));
    PIMAGEHLP_SYMBOL64 psymbol = reinterpret_cast<PIMAGEHLP_SYMBOL64>(symbol_buffer);
    psymbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
    psymbol->MaxNameLength = 512;
    if (SymGetSymFromAddr64(process, static_cast<DWORD64>(reinterpret_cast<DWORD_PTR>(pointer)), &displacement, psymbol))
    {
		if (name)
		{
			WWFormat::Into(name, nameSize, "{}();", psymbol->Name);
		}

		// Get line now
		if (gsSymGetLineFromAddr)
		{

			IMAGEHLP_LINE64 line;
			memset(&line,0,sizeof(line));
			line.SizeOfStruct = sizeof(line);
			DWORD lineDisplacement = 0;
			if (gsSymGetLineFromAddr(process, static_cast<DWORD64>(reinterpret_cast<DWORD_PTR>(pointer)), &lineDisplacement, &line))
			{
				if (filename)
				{
					WWFormat::Into(filename, filenameSize, "{}", line.FileName);
				}
				if (linenumber)
				{
					*linenumber = static_cast<unsigned int>(line.LineNumber);
				}
				if (address)
				{
					*address = static_cast<unsigned int>(line.Address);
				}
			} 					
		}
    }
}


//*****************************************************************************
// Gets last x addresses from the stack
//*****************************************************************************
void FillStackAddresses(void**addresses, unsigned int count, unsigned int skip)
{
	// RtlCaptureStackBackTrace reads the return addresses directly from the stack, without symbols
	// or module list; they are resolved only on output (StackDumpFromAddresses). This keeps
	// MEMORYPOOL_STACKTRACE (call stack per memory block for the leak report) fast enough; StackWalk64
	// with symbol access per block is far too slow. One extra frame is skipped because this function
	// itself is on the stack.
	USHORT captured = ::RtlCaptureStackBackTrace(static_cast<ULONG>(skip) + 1, static_cast<ULONG>(count), addresses, nullptr);
	for (unsigned int i = captured; i < count; ++i)
	{
		addresses[i] = nullptr;
	}
}



//*****************************************************************************
// Do full stack dump using an address array
//*****************************************************************************
void StackDumpFromAddresses(void**addresses, unsigned int count, void (*callback)(const char *))
{
	if (callback == nullptr) 
	{
		callback = StackDumpDefaultHandler;
	}

	InitSymbolInfo();

	while ((count--) && (*addresses!=nullptr))
	{
		WriteStackLine(*addresses,callback);	
		addresses++;
	}	
}


AsciiString g_LastErrorDump;
//*****************************************************************************
//*****************************************************************************
void WriteStackLine(void*address, void (*callback)(const char*))
{
	static char line[MAX_PATH];
	static char function_name[512];
	static char filename[MAX_PATH];
	unsigned int linenumber;
	unsigned int addr;

	GetFunctionDetails(address, function_name, sizeof(function_name), filename, sizeof(filename), &linenumber, &addr);
    WWFormat::Into(line, "  {}({:d}) : {} 0x{:016X}", filename, linenumber, function_name, reinterpret_cast<std::uintptr_t>(address));
		if (g_LastErrorDump.isNotEmpty()) {
			g_LastErrorDump.concat(line);
			g_LastErrorDump.concat("\n");
		}
	callback(line);
	callback("\n");
}


//*****************************************************************************
//*****************************************************************************
void DumpExceptionInfo( unsigned int u, EXCEPTION_POINTERS* e_info )
{
	// One folder of files per crash: minidump with all threads, Info.txt (build, map, frame), the
	// log ring buffer and the replay of the running game. Written first, before anything else can
	// go wrong in the broken process.
	{
		char reason[64];
		WWFormat::Into(reason, "exception 0x{:08X}", u);
		char folder[MAX_PATH];
		if( WriteCrashReport( reason, e_info, folder, sizeof(folder) ) )
			DEBUG_LOG(( "crash report in %s\n", folder ));
	}

   DEBUG_LOG(( "\n********** EXCEPTION DUMP ****************\n" ));
	/*
	** List of possible exceptions
	*/
	 g_LastErrorDump.clear(); 

	static const unsigned int _codes[] = {
		EXCEPTION_ACCESS_VIOLATION,
		EXCEPTION_ARRAY_BOUNDS_EXCEEDED,
		EXCEPTION_BREAKPOINT,
		EXCEPTION_DATATYPE_MISALIGNMENT,
		EXCEPTION_FLT_DENORMAL_OPERAND,
		EXCEPTION_FLT_DIVIDE_BY_ZERO,
		EXCEPTION_FLT_INEXACT_RESULT,
		EXCEPTION_FLT_INVALID_OPERATION,
		EXCEPTION_FLT_OVERFLOW,
		EXCEPTION_FLT_STACK_CHECK,
		EXCEPTION_FLT_UNDERFLOW,
		EXCEPTION_ILLEGAL_INSTRUCTION,
		EXCEPTION_IN_PAGE_ERROR,
		EXCEPTION_INT_DIVIDE_BY_ZERO,
		EXCEPTION_INT_OVERFLOW,
		EXCEPTION_INVALID_DISPOSITION,
		EXCEPTION_NONCONTINUABLE_EXCEPTION,
		EXCEPTION_PRIV_INSTRUCTION,
		EXCEPTION_SINGLE_STEP,
		EXCEPTION_STACK_OVERFLOW,
		0xffffffff
	};

	/*
	** Information about each exception type.
	*/
	static char const * _code_txt[] = {
		"Error code: EXCEPTION_ACCESS_VIOLATION\nDescription: The thread tried to read from or write to a virtual address for which it does not have the appropriate access.",
		"Error code: EXCEPTION_ARRAY_BOUNDS_EXCEEDED\nDescription: The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.",
		"Error code: EXCEPTION_BREAKPOINT\nDescription: A breakpoint was encountered.",
		"Error code: EXCEPTION_DATATYPE_MISALIGNMENT\nDescription: The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.",
		"Error code: EXCEPTION_FLT_DENORMAL_OPERAND\nDescription: One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.",
		"Error code: EXCEPTION_FLT_DIVIDE_BY_ZERO\nDescription: The thread tried to divide a floating-point value by a floating-point divisor of zero.",
		"Error code: EXCEPTION_FLT_INEXACT_RESULT\nDescription: The result of a floating-point operation cannot be represented exactly as a decimal fraction.",
		"Error code: EXCEPTION_FLT_INVALID_OPERATION\nDescription: Some strange unknown floating point operation was attempted.",
		"Error code: EXCEPTION_FLT_OVERFLOW\nDescription: The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.",
		"Error code: EXCEPTION_FLT_STACK_CHECK\nDescription: The stack overflowed or underflowed as the result of a floating-point operation.",
		"Error code: EXCEPTION_FLT_UNDERFLOW\nDescription:	The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.",
		"Error code: EXCEPTION_ILLEGAL_INSTRUCTION\nDescription:	The thread tried to execute an invalid instruction.",
		"Error code: EXCEPTION_IN_PAGE_ERROR\nDescription:	The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.",
		"Error code: EXCEPTION_INT_DIVIDE_BY_ZERO\nDescription: The thread tried to divide an integer value by an integer divisor of zero.",
		"Error code: EXCEPTION_INT_OVERFLOW\nDescription: The result of an integer operation caused a carry out of the most significant bit of the result.",
		"Error code: EXCEPTION_INVALID_DISPOSITION\nDescription: An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.",
		"Error code: EXCEPTION_NONCONTINUABLE_EXCEPTION\nDescription: The thread tried to continue execution after a noncontinuable exception occurred.",
		"Error code: EXCEPTION_PRIV_INSTRUCTION\nDescription: The thread tried to execute an instruction whose operation is not allowed in the current machine mode.",
		"Error code: EXCEPTION_SINGLE_STEP\nDescription: A trace trap or other single-instruction mechanism signaled that one instruction has been executed.",
		"Error code: EXCEPTION_STACK_OVERFLOW\nDescription: The thread used up its stack.",
		"Error code: ?????\nDescription: Unknown exception."
	};

	DEBUG_LOG( ("Dump exception info\n") );
	CONTEXT *context = e_info->ContextRecord;
	/*
	** The following are set for access violation only
	*/
	int access_read_write=-1;
	unsigned long access_address = 0;
	AsciiString msg; 

// DOUBLE_DEBUG does a DEBUG_LOG, and concats to g_LastErrorDump.  jba.
#define DOUBLE_DEBUG(x) { msg.format x; g_LastErrorDump.concat(msg); DEBUG_LOG( x ); }

	if ( e_info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION )
	{
		DOUBLE_DEBUG (("Exception is access violation\n"));
		access_read_write = static_cast<int>(e_info->ExceptionRecord->ExceptionInformation[0]);  // 0=read, 1=write
		access_address = static_cast<unsigned long>(e_info->ExceptionRecord->ExceptionInformation[1]);
	}
	else
	{
		DOUBLE_DEBUG (("Exception code is %x\n", static_cast<unsigned>(e_info->ExceptionRecord->ExceptionCode)));
	}
	void *winMainAddr = reinterpret_cast<void *>(WinMain);
	DOUBLE_DEBUG(("WinMain at %p\n", winMainAddr));
	/*
	** Match the exception type with the error string and print it out
	*/
	int i;
	for ( i=0 ; _codes[i] != 0xffffffff ; i++ )
	{
		if ( _codes[i] == e_info->ExceptionRecord->ExceptionCode )
		{
			DEBUG_LOG ( ("Found exception description\n") );
			break;
		}
	}
	DOUBLE_DEBUG( ("%s\n", _code_txt[i]));
	/** For access violations, print out the violation address and if it was read or write.
	*/
	if ( e_info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION )
	{
		if ( access_read_write )
		{
			DOUBLE_DEBUG( ("Access address:%08X was written to.\n", static_cast<unsigned>(access_address)));
		}
		else
		{
			DOUBLE_DEBUG( ("Access address:%08X was read from.\n", static_cast<unsigned>(access_address)));
		}
	}

	DOUBLE_DEBUG (("\nStack Dump:\n"));
	StackDumpFromContext(context, nullptr);

	DOUBLE_DEBUG (("\nDetails:\n"));

	DOUBLE_DEBUG (("Register dump...\n"));

	/*
	** Dump the registers.
	*/
	DOUBLE_DEBUG ( ( "Rip:%016llX\tRsp:%016llX\tRbp:%016llX\n", context->Rip, context->Rsp, context->Rbp));
	DOUBLE_DEBUG ( ( "Rax:%016llX\tRbx:%016llX\tRcx:%016llX\n", context->Rax, context->Rbx, context->Rcx));
	DOUBLE_DEBUG ( ( "Rdx:%016llX\tRsi:%016llX\tRdi:%016llX\n", context->Rdx, context->Rsi, context->Rdi));
	DOUBLE_DEBUG ( ( "R8:%016llX\tR9:%016llX\tR10:%016llX\tR11:%016llX\n", context->R8, context->R9, context->R10, context->R11));
	DOUBLE_DEBUG ( ( "R12:%016llX\tR13:%016llX\tR14:%016llX\tR15:%016llX\n", context->R12, context->R13, context->R14, context->R15));
	DOUBLE_DEBUG ( ( "EFlags:%08X \n", static_cast<unsigned>(context->EFlags)));
	DOUBLE_DEBUG ( ( "CS:%04x  SS:%04x  DS:%04x  ES:%04x  FS:%04x  GS:%04x\n", context->SegCs, context->SegSs, context->SegDs, context->SegEs, context->SegFs, context->SegGs));

	/*
	** Dump the bytes at EIP. This will make it easier to match the crash address with later versions of the game.
	*/
	char scrap[512];
	DOUBLE_DEBUG ( ("EIP bytes dump...\n"));
	WWFormat::Into(scrap, "\nBytes at CS:EIP ({:016X})  : ", static_cast<std::uintptr_t>(static_cast<DWORD_PTR>(CTX_IP(*context))));
	unsigned char *eip_ptr = reinterpret_cast<unsigned char *>(static_cast<DWORD_PTR>(CTX_IP(*context)));
	char bytestr[32];

	for (int c = 0 ; c < 32 ; c++)
	{
		if (IsBadReadPtr(eip_ptr, 1))
		{
			safe_strcat(scrap, "?? ");
		}
		else
		{
			WWFormat::Into(bytestr, "{:02X} ", *eip_ptr);
			safe_strcat (scrap, bytestr);
		}
		eip_ptr++;
	}

	safe_strcat (scrap, "\n");
	DOUBLE_DEBUG ( ( "%s", scrap));
  DEBUG_LOG(( "********** END EXCEPTION DUMP ****************\n\n" ));
}																									 


#pragma pack(pop)

#endif

