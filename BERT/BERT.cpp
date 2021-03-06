/*
 * Basic Excel R Toolkit (BERT)
 * Copyright (C) 2014-2016 Structured Data, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#include "stdafx.h"
#include "BERT.h"

#include "ThreadLocalStorage.h"

#include "RInterface.h"
#include "Dialogs.h"
//#include "Console.h"
#include "resource.h"
//#include <Richedit.h>

#include "RegistryUtils.h"

#include <shellapi.h>
#include <strsafe.h>

#include "RemoteShell.h"

std::vector < double > functionEntries;
std::list< std::string > loglist;

HWND hWndConsole = 0;

/* mutex for locking the console message history */
HANDLE muxLogList = 0;

std::string strresult;
AutocompleteData autocomplete;

extern void FreeStream();
extern void SetExcelPtr( LPVOID p, LPVOID ribbon );

LPXLOPER12 BERTFunctionCall( 
	int index
	, LPXLOPER12 input_0
	, LPXLOPER12 input_1
	, LPXLOPER12 input_2
	, LPXLOPER12 input_3
	, LPXLOPER12 input_4
	, LPXLOPER12 input_5
	, LPXLOPER12 input_6
	, LPXLOPER12 input_7
	, LPXLOPER12 input_8
	, LPXLOPER12 input_9
	, LPXLOPER12 input_10
	, LPXLOPER12 input_11
	, LPXLOPER12 input_12
	, LPXLOPER12 input_13
	, LPXLOPER12 input_14
	, LPXLOPER12 input_15
	){

	XLOPER12 * rslt = get_thread_local_xloper12();
	resetXlOper( rslt );

	rslt->xltype = xltypeErr;
	rslt->val.err = xlerrName;

	if (index < 0 || index >= RFunctions.size()) return rslt;

//	RFUNCDESC func = RFunctions[index];
	RFuncDesc2 func = RFunctions[index];

	std::vector< LPXLOPER12 > args;

	// in order to support (...) args, we should allow
	// any number of args.  register every function with 
	// the maximum, and check here for null rather than
	// checking the arity.

	// check for xltypeMissing, not null, although there might 
	// be omitted arguments... 
	
	// we could handle functions with (...) specially, rather
	// than requiring this work for every function.  still
	// it shouldn't be that expensive... although there are
	// better ways to handle it (count and remove >1)

	args.push_back(input_0);
	args.push_back(input_1);
	args.push_back(input_2);
	args.push_back(input_3);
	args.push_back(input_4);
	args.push_back(input_5);
	args.push_back(input_6);
	args.push_back(input_7);
	args.push_back(input_8);
	args.push_back(input_9);
	args.push_back(input_10);
	args.push_back(input_11);
	args.push_back(input_12);
	args.push_back(input_13);
	args.push_back(input_14);
	args.push_back(input_15);

	while (args.size() > 0 && args[args.size() - 1]->xltype == xltypeMissing) args.pop_back();

	//RExec2(rslt, func[0].first, args);
	RExec4(rslt, func, args);

	return rslt;
}

LPXLOPER12 BERT_RCall(
	LPXLOPER12 func
	, LPXLOPER12 input_0
	, LPXLOPER12 input_1
	, LPXLOPER12 input_2
	, LPXLOPER12 input_3
	, LPXLOPER12 input_4
	, LPXLOPER12 input_5
	, LPXLOPER12 input_6
	, LPXLOPER12 input_7 )
{
	XLOPER12 * rslt = get_thread_local_xloper12();
	resetXlOper(rslt);

	rslt->xltype = xltypeErr;
	rslt->val.err = xlerrName;

	std::string funcNarrow;

	if (func->xltype == xltypeStr)
	{
		// FIXME: this seems fragile, as it might try to 
		// allocate a really large contiguous block.  better
		// to walk through and find lines?

		int len = WideCharToMultiByte(CP_UTF8, 0, &(func->val.str[1]), func->val.str[0], 0, 0, 0, 0);
		char *ps = new char[len + 1];
		WideCharToMultiByte(CP_UTF8, 0, &(func->val.str[1]), func->val.str[0], ps, len, 0, 0);
		ps[len] = 0;
		funcNarrow = ps;
	}
	else return rslt;

	std::vector< LPXLOPER12 > args;
	args.push_back(input_0);
	args.push_back(input_1);
	args.push_back(input_2);
	args.push_back(input_3);
	args.push_back(input_4);
	args.push_back(input_5);
	args.push_back(input_6);
	args.push_back(input_7);
	while (args.size() > 0 && args[args.size() - 1]->xltype == xltypeMissing) args.pop_back();
	RExec2(rslt, funcNarrow, args);
	return rslt;
}

void logMessage(const char *buf, int len, bool console)
{
	std::string entry;

	if (!strncmp(buf, WRAP_ERR, sizeof(WRAP_ERR)-1)) // ??
	{
		entry = "Error:";
		entry += (buf + sizeof(WRAP_ERR)-1);
	}
	else entry = buf;

	DWORD stat = WaitForSingleObject(muxLogList, INFINITE);
	if (stat == WAIT_OBJECT_0)
	{
		loglist.push_back(entry);
		while (loglist.size() > MAX_LOGLIST_SIZE) loglist.pop_front();
		ReleaseMutex(muxLogList);
	}

	if( console ) rshell_send(entry.c_str());

	/*
	if (console && hWndConsole)
	{
		::SendMessage(hWndConsole, WM_APPEND_LOG, 0, (LPARAM)entry.c_str());
	}
	*/
}

void resetXlOper(LPXLOPER12 x)
{
	if ( x->xltype == (xltypeStr | xlbitDLLFree) && x->val.str)
	{
		// we pass a static string with zero length -- don't delete that

		if( x->val.str[0] )
			delete[] x->val.str;
		x->val.str = 0;
	}
	else if ((x->xltype == xltypeMulti || x->xltype == ( xltypeMulti | xlbitDLLFree )) && x->val.array.lparray)
	{
		// have to consider the case that there are strings
		// in the array, or even nested multis (not that that
		// is a useful thing to do, but it could happen)

		int len = x->val.array.columns * x->val.array.rows;
		for (int i = 0; i < len; i++) resetXlOper(&(x->val.array.lparray[i]));

		delete[] x->val.array.lparray;
		x->val.array.lparray = 0;
	}

	x->val.err = xlerrNull;
	x->xltype = xltypeNil;

}

LPXLOPER BERT_Volatile(LPXLOPER arg)
{
	return arg;
}

short BERT_Reload()
{
	ClearFunctions();
	LoadStartupFile();
	MapFunctions();
	RegisterAddinFunctions();

	return 1;
}

/**
 * open the home directory.  note that this is thread-safe,
 * it can be called directly from the threaded console (and it is)
 */
short BERT_HomeDirectory()
{
	char buffer[MAX_PATH];
	if (!CRegistryUtils::GetRegExpandString(HKEY_CURRENT_USER, buffer, MAX_PATH - 1, REGISTRY_KEY, REGISTRY_VALUE_R_USER))
		ExpandEnvironmentStringsA(DEFAULT_R_USER, buffer, MAX_PATH);
	::ShellExecuteA(NULL, "open", buffer, NULL, NULL, SW_SHOWDEFAULT);
	return 1;
}

/**
 * set the excel callback pointer.
 */
int BERT_SetPtr( LPVOID pdisp, LPVOID pribbon )
{
	SetExcelPtr(pdisp, pribbon);
	return 2;
}

short BERT_Configure()
{
	// ::MessageBox(0, L"No", L"Options", MB_OKCANCEL | MB_ICONINFORMATION);

	XLOPER12 xWnd;
	Excel12(xlGetHwnd, &xWnd, 0);

	// InitRichEdit();

	::DialogBox(ghModule,
		MAKEINTRESOURCE(IDD_CONFIG),
		(HWND)xWnd.val.w,
		(DLGPROC)OptionsDlgProc);

	Excel12(xlFree, 0, 1, (LPXLOPER12)&xWnd);

	return 1;
}

/*
void ClearConsole()
{
	if (hWndConsole)
	{
		::PostMessageA(hWndConsole, WM_COMMAND, MAKEWPARAM(WM_CLEAR_BUFFER, 0), 0);
	}
}

void CloseConsole()
{
	if (hWndConsole)
	{
		::PostMessageA(hWndConsole, WM_COMMAND, MAKEWPARAM(WM_CLOSE_CONSOLE,0), 0);
	}
}

void CloseConsoleAsync()
{
	if (hWndConsole)
	{
		::PostMessageA(hWndConsole, WM_COMMAND, MAKEWPARAM(WM_CLOSE_CONSOLE_ASYNC, 0), 0);
	}
}
*/

short BERT_About()
{
	XLOPER12 xWnd;
	Excel12(xlGetHwnd, &xWnd, 0);

	::DialogBox(ghModule,
		MAKEINTRESOURCE(IDD_ABOUT),
		(HWND)xWnd.val.w,
		(DLGPROC)AboutDlgProc);

	Excel12(xlFree, 0, 1, (LPXLOPER12)&xWnd);
	return 1;
}

/**
 * send text to (or clear text in) the excel status bar, via 
 * the Excel API.  this must be called from within an Excel call
 * context.
 */
void ExcelStatus(const char *message)
{
	XLOPER12 xlUpdate;
	XLOPER12 xlMessage;

	xlUpdate.xltype = xltypeBool;

	if (!message)
	{
		xlUpdate.val.xbool = false;
		Excel12(xlcMessage, 0, 1, &xlUpdate );
	}
	else
	{
		int len = MultiByteToWideChar(CP_UTF8, 0, message, -1, 0, 0);

		xlUpdate.val.xbool = true;

		xlMessage.xltype = xltypeStr | xlbitXLFree;
		xlMessage.val.str = new XCHAR[len + 2];
		xlMessage.val.str[0] = len == 0 ? 0 : len - 1;

		MultiByteToWideChar(CP_UTF8, 0, message, -1, &(xlMessage.val.str[1]), len+1);

		Excel12(xlcMessage, 0, 2, &xlUpdate, &xlMessage);
	}

}


void clearLogText()
{
	DWORD stat = WaitForSingleObject(muxLogList, INFINITE);
	if (stat == WAIT_OBJECT_0)
	{
		loglist.clear();
		ReleaseMutex(muxLogList);
	}
}

void getLogText(std::list< std::string > &list)
{
	DWORD stat = WaitForSingleObject(muxLogList, INFINITE);
	if (stat == WAIT_OBJECT_0)
	{
		for (std::list< std::string> ::iterator iter = loglist.begin();
			iter != loglist.end(); iter++)
		{
			list.push_back(*iter);
		}
		ReleaseMutex(muxLogList);
	}
}

short BERT_Console()
{
	open_remote_shell();
	return 1;
}

void SysCleanup()
{
	if (hWndConsole)
	{
		::CloseWindow(hWndConsole);
		::SendMessage(hWndConsole, WM_DESTROY, 0, 0);
	}

	RShutdown();
	FreeStream();

	CloseHandle(muxLogList);

}

void SysInit()
{

	muxLogList = CreateMutex(0, 0, 0);

	if (RInit()) return;

	// loop through the function table and register the functions

	RegisterBasicFunctions();
	RegisterAddinFunctions();

}

/**
 * this is the second part of the context-switching callback
 * used when running functions from the console.  it is only
 * called (indirectly) by the SafeCall function.
 *
 * FIXME: reorganize this around cmdid, clearer switching
 */
long BERT_SafeCall(long cmdid, LPXLOPER12 xl, LPXLOPER xl2)
{
	SVECTOR sv;
	bool excludeFlag = false;

	if (cmdid == 0x08) {
		userBreak();
	}
	else if (xl->xltype & xltypeStr)
	{
		std::string func;
		NarrowString(func, xl);

		if (cmdid == 4) {
			return notifyWatch(func);
		}
		else if (cmdid == 2) {
			return -1; // getNames(moneyList, func);
		}
		else if (cmdid == 10) {
			sv.push_back(func);
			excludeFlag = true;
		}
		else if (cmdid == 12) {
			int caret = xl2->val.num;
			return getAutocomplete( autocomplete, func, caret);
			// return 0;
		}
		else {
			return -1; // getCallTip(calltip, func);
		}
	}
	else if (xl->xltype & xltypeMulti)
	{
		// excel seems to prefer columns for one dimension,
		// but it doesn't actually matter as it's passed
		// as a straight vector

		int r = xl->val.array.rows;
		int c = xl->val.array.columns;
		int m = r * c;

		for (int i = 0; i < m; i++)
		{
			std::string str;
			NarrowString(str, &(xl->val.array.lparray[i]));
			sv.push_back(str);
		}
	}
	if (sv.size())
	{
		PARSE_STATUS_2 ps2;
		int err;
		if( cmdid == 1 ) RExecVector(sv, &err, &ps2, false, true, &strresult); // internal 
		else RExecVector(sv, &err, &ps2, true, excludeFlag);
		return ps2;
	}
	return PARSE2_EOF;
}

/**
 * this is not in util because it uses the Excel API,
 * and util should be clean.  FIXME: split into generic
 * and XLOPER-specific versions.
 */
void NarrowString(std::string &out, LPXLOPER12 pxl)
{
	int i, len = pxl->val.str[0];
	int slen = WideCharToMultiByte(CP_UTF8, 0, &(pxl->val.str[1]), len, 0, 0, 0, 0);
	char *s = new char[slen + 1];
	WideCharToMultiByte(CP_UTF8, 0, &(pxl->val.str[1]), len, s, slen, 0, 0);
	s[slen] = 0;
	out = s;
	delete[] s;
}

LPXLOPER12 BERT_UpdateScript(LPXLOPER12 script)
{
	XLOPER12 * rslt = get_thread_local_xloper12();

	rslt->xltype = xltypeErr;
	rslt->val.err = xlerrValue;

	if (script->xltype == xltypeStr)
	{
		ClearFunctions();

		std::string str;
		NarrowString(str, script);

		if (!UpdateR(str))
		{
			rslt->xltype = xltypeBool;
			rslt->val.xbool = true;
			MapFunctions();
			RegisterAddinFunctions();
		}
		else
		{
			rslt->val.err = xlerrValue;
		}
	}

	return rslt;
}


void UnregisterFunctions()
{
	XLOPER xlRegisterID;

	for (std::vector< double > ::iterator iter = functionEntries.begin(); iter != functionEntries.end(); iter++ )
	{
		xlRegisterID.xltype = xltypeNum;
		xlRegisterID.val.num = *iter;
		Excel12(xlfUnregister, 0, 1, &xlRegisterID);
	}

	functionEntries.clear();

}


bool RegisterBasicFunctions()
{
	LPXLOPER12 xlParm[32];
	XLOPER12 xlRegisterID;

	int err;

	static bool fRegisteredOnce = false;

	char szHelpBuffer[512] = " ";
	bool fExcel12 = false;

	// init memory

	for (int i = 0; i< 32; i++) xlParm[i] = new XLOPER12;

	// get the library; store as the first entry in our parameter list

	Excel12(xlGetName, xlParm[0], 0);

	for (int i = 0; funcTemplates[i][0]; i++)
	{
		for (int j = 0; j < 15; j++)
		{
			int len = wcslen(funcTemplates[i][j]);
			xlParm[j + 1]->xltype = xltypeStr;
			xlParm[j + 1]->val.str = new XCHAR[len + 2];

			// strcpy_s(xlParm[j + 1]->val.str + 1, len + 1, funcTemplates[i][j]);
			// for (int k = 0; k < len; k++) xlParm[j + 1]->val.str[k + 1] = funcTemplates[i][j][k];

			wcscpy_s(&(xlParm[j + 1]->val.str[1]), len + 1, funcTemplates[i][j]);

			xlParm[j + 1]->val.str[0] = len;
		}

		xlRegisterID.xltype = xltypeMissing;
		err = Excel12v(xlfRegister, &xlRegisterID, 16, xlParm);

		Excel12(xlFree, 0, 1, &xlRegisterID);

		for (int j = 0; j < 15; j++)
		{
			delete[] xlParm[j + 1]->val.str;
		}

	}

	// clean up (don't forget to free the retrieved dll xloper in parm 0)

	Excel12(xlFree, 0, 1, xlParm[0]);

	for (int i = 0; i< 32; i++) delete xlParm[i];


	// debugLogf("Exit registerAddinFunctions\n");

	// set state and return

	// CRASHER for crash (recovery) testing // Excel4( xlFree, 0, 1, 1000 );

	fRegisteredOnce = true;
	return true;
}

bool RegisterAddinFunctions()
{

//	LPXLOPER12 xlParm[11 + MAX_ARGUMENT_COUNT];

	LPXLOPER12 *xlParm;
	XLOPER12 xlRegisterID;

	int err;
	int alistCount = 12 + MAX_ARGUMENT_COUNT;

	static bool fRegisteredOnce = false;

	//char szHelpBuffer[512] = " ";
	WCHAR wbuffer[512];
	WCHAR wtmp[64];
	int tlen;
	bool fExcel12 = false;

	// init memory

	xlParm = new LPXLOPER12[alistCount];
	for (int i = 0; i< alistCount; i++) xlParm[i] = new XLOPER12;

	// get the library; store as the first entry in our parameter list

	Excel12( xlGetName, xlParm[0], 0 );

	UnregisterFunctions();

	// { "BERTFunctionCall", "UU", "BERTTest", "Input", "1", "BERT", "", "100", "Test function", "", "", "", "", "", "", "" },

	int fcount = RFunctions.size();

	for (int i = 0; i< fcount && i< MAX_FUNCTION_COUNT; i++)
	{
		//RFUNCDESC func = RFunctions[i];
		RFuncDesc2 func = RFunctions[i];
		int scount = 0;

		for (int j = 1; j < alistCount; j++)
		{
			xlParm[j]->xltype = xltypeMissing;
		}

		for (scount = 0; scount < 9; scount++)
		{
			switch (scount)
			{
			case 0:  
				StringCbPrintf( wbuffer, 256, L"BERTFunctionCall%04d", 1000 + i); 
				break;
			case 1: 
				//for (int k = 0; k < func.size(); k++) szHelpBuffer[k] = 'U';
				//szHelpBuffer[func.size()] = 0;
				StringCbPrintf(wbuffer, 256, L"UUUUUUUUUUUUUUUUU");
				break;
			case 2:

				// FIXME: this is correct, if messy, but unecessary: tokens in R can't 
				// use unicode.  if there are high-byte characters in here the function 
				// name is illegal. [FIXME: is that true? I don't think so].

				tlen = MultiByteToWideChar(CP_UTF8, 0, func.pairs[0].first.c_str(), -1, 0, 0);
				if (tlen > 0) MultiByteToWideChar(CP_UTF8, 0, func.pairs[0].first.c_str(), -1, wtmp, 64);
				else wtmp[0] = 0;
				StringCbPrintf(wbuffer, 256, L"R.%s", wtmp); 
				break;
			case 3: 
				wbuffer[0] = 0;
				for (int k = 1; k < func.pairs.size(); k++)
				{
					tlen = MultiByteToWideChar(CP_UTF8, 0, func.pairs[k].first.c_str(), -1, 0, 0);
					if (tlen > 0) MultiByteToWideChar(CP_UTF8, 0, func.pairs[k].first.c_str(), -1, wtmp, 64);
					else wtmp[0] = 0;
					
					if (wcslen(wbuffer)) StringCbCat(wbuffer, 256, L",");
					StringCbCat(wbuffer, 256, wtmp);
				}
				break;
			case 4: StringCbPrintf(wbuffer, 256, L"1"); break;
			case 5: 
				if (func.function_category.length() > 0) {
					tlen = MultiByteToWideChar(CP_UTF8, 0, func.function_category.c_str(), -1, 0, 0);
					if (tlen > 255) tlen = 255;
					if (tlen > 0) MultiByteToWideChar(CP_UTF8, 0, func.function_category.c_str(), -1, wbuffer, 256);
					wbuffer[tlen] = 0;
				}
				else StringCbPrintf(wbuffer, 256, L"Exported R functions"); 
				break;
			case 7: StringCbPrintf(wbuffer, 256, L"%d", 100 + i); break;
			case 8: 
				if (func.function_description.length() > 0) {
					tlen = MultiByteToWideChar(CP_UTF8, 0, func.function_description.c_str(), -1, 0, 0);
					if (tlen > 255) tlen = 255;
					if (tlen > 0) MultiByteToWideChar(CP_UTF8, 0, func.function_description.c_str(), -1, wbuffer, 256);
					wbuffer[tlen] = 0;
				}
				else StringCbPrintf(wbuffer, 256, L"Exported R function"); 
				break; // ??

			default: StringCbPrintf(wbuffer, 256, L""); break;
			}
			
			int len = wcslen(wbuffer);
			xlParm[scount + 1]->xltype = xltypeStr ;
			xlParm[scount + 1]->val.str = new XCHAR[len + 2];

			for (int k = 0; k < len; k++) xlParm[scount + 1]->val.str[k + 1] = wbuffer[k];
			xlParm[scount + 1]->val.str[0] = len;

		}
			

		// so this is supposed to show the default value; but for some
		// reason, it's truncating some strings (one string?) - FALS.  leave
		// it out until we can figure this out.

		// also, quote default strings.

		for (int j = 0; j < func.pairs.size() - 1 && j< MAX_ARGUMENT_COUNT; j++)
		{
			int len = MultiByteToWideChar(CP_UTF8, 0, func.pairs[j + 1].second.c_str(), -1, 0, 0);
			xlParm[scount + 1]->xltype = xltypeStr;
			xlParm[scount + 1]->val.str = new XCHAR[len + 2];
			xlParm[scount + 1]->val.str[0] = len > 0 ? len-1 : 0;
			MultiByteToWideChar(CP_UTF8, 0, func.pairs[j + 1].second.c_str(), -1, &(xlParm[scount + 1]->val.str[1]), len+1);
			scount++;
		}
		
		// it seems that it always truncates the last character of the last argument
		// help string, UNLESS there's another parameter.  so here you go.  could just
		// have an extra missing? CHECK / TODO / FIXME

		xlParm[scount + 1]->xltype = xltypeStr;
		xlParm[scount + 1]->val.str = new XCHAR[2];
		xlParm[scount + 1]->val.str[0] = 0;
		scount++;

		xlRegisterID.xltype = xltypeMissing;
		err = Excel12v(xlfRegister, &xlRegisterID, scount + 1, xlParm);
		if (xlRegisterID.xltype == xltypeNum)
		{
			functionEntries.push_back(xlRegisterID.val.num);
		}
		Excel12(xlFree, 0, 1, &xlRegisterID);

		for (int j = 1; j < alistCount; j++)
		{
			if (xlParm[j]->xltype == xltypeStr)
			{
				delete[] xlParm[j]->val.str;
			}
			xlParm[j]->xltype = xltypeMissing;
		}

	}
	
	// clean up (don't forget to free the retrieved dll xloper in parm 0)

	Excel12(xlFree, 0, 1, xlParm[0]);

	for (int i = 0; i< alistCount; i++) delete xlParm[i];
	delete [] xlParm;

	// debugLogf("Exit registerAddinFunctions\n");

	// set state and return

	// CRASHER for crash (recovery) testing // Excel4( xlFree, 0, 1, 1000 );

	fRegisteredOnce = true;
	return true;
}

#ifdef _DEBUG

int DebugOut(const char *fmt, ...)
{
	static char msg[1024 * 32]; // ??
	int ret;
	va_list args;
	va_start(args, fmt);

	ret = vsprintf_s(msg, fmt, args);
	OutputDebugStringA(msg);

	va_end(args);
	return ret;
}

#endif

// placeholder functions follow

BFC(1000);
BFC(1001);
BFC(1002);
BFC(1003);
BFC(1004);
BFC(1005);
BFC(1006);
BFC(1007);
BFC(1008);
BFC(1009);
BFC(1010);
BFC(1011);
BFC(1012);
BFC(1013);
BFC(1014);
BFC(1015);
BFC(1016);
BFC(1017);
BFC(1018);
BFC(1019);
BFC(1020);
BFC(1021);
BFC(1022);
BFC(1023);
BFC(1024);
BFC(1025);
BFC(1026);
BFC(1027);
BFC(1028);
BFC(1029);
BFC(1030);
BFC(1031);
BFC(1032);
BFC(1033);
BFC(1034);
BFC(1035);
BFC(1036);
BFC(1037);
BFC(1038);
BFC(1039);
BFC(1040);
BFC(1041);
BFC(1042);
BFC(1043);
BFC(1044);
BFC(1045);
BFC(1046);
BFC(1047);
BFC(1048);
BFC(1049);
BFC(1050);
BFC(1051);
BFC(1052);
BFC(1053);
BFC(1054);
BFC(1055);
BFC(1056);
BFC(1057);
BFC(1058);
BFC(1059);
BFC(1060);
BFC(1061);
BFC(1062);
BFC(1063);
BFC(1064);
BFC(1065);
BFC(1066);
BFC(1067);
BFC(1068);
BFC(1069);
BFC(1070);
BFC(1071);
BFC(1072);
BFC(1073);
BFC(1074);
BFC(1075);
BFC(1076);
BFC(1077);
BFC(1078);
BFC(1079);
BFC(1080);
BFC(1081);
BFC(1082);
BFC(1083);
BFC(1084);
BFC(1085);
BFC(1086);
BFC(1087);
BFC(1088);
BFC(1089);
BFC(1090);
BFC(1091);
BFC(1092);
BFC(1093);
BFC(1094);
BFC(1095);
BFC(1096);
BFC(1097);
BFC(1098);
BFC(1099);
BFC(1100);
BFC(1101);
BFC(1102);
BFC(1103);
BFC(1104);
BFC(1105);
BFC(1106);
BFC(1107);
BFC(1108);
BFC(1109);
BFC(1110);
BFC(1111);
BFC(1112);
BFC(1113);
BFC(1114);
BFC(1115);
BFC(1116);
BFC(1117);
BFC(1118);
BFC(1119);
BFC(1120);
BFC(1121);
BFC(1122);
BFC(1123);
BFC(1124);
BFC(1125);
BFC(1126);
BFC(1127);
BFC(1128);
BFC(1129);
BFC(1130);
BFC(1131);
BFC(1132);
BFC(1133);
BFC(1134);
BFC(1135);
BFC(1136);
BFC(1137);
BFC(1138);
BFC(1139);
BFC(1140);
BFC(1141);
BFC(1142);
BFC(1143);
BFC(1144);
BFC(1145);
BFC(1146);
BFC(1147);
BFC(1148);
BFC(1149);
BFC(1150);
BFC(1151);
BFC(1152);
BFC(1153);
BFC(1154);
BFC(1155);
BFC(1156);
BFC(1157);
BFC(1158);
BFC(1159);
BFC(1160);
BFC(1161);
BFC(1162);
BFC(1163);
BFC(1164);
BFC(1165);
BFC(1166);
BFC(1167);
BFC(1168);
BFC(1169);
BFC(1170);
BFC(1171);
BFC(1172);
BFC(1173);
BFC(1174);
BFC(1175);
BFC(1176);
BFC(1177);
BFC(1178);
BFC(1179);
BFC(1180);
BFC(1181);
BFC(1182);
BFC(1183);
BFC(1184);
BFC(1185);
BFC(1186);
BFC(1187);
BFC(1188);
BFC(1189);
BFC(1190);
BFC(1191);
BFC(1192);
BFC(1193);
BFC(1194);
BFC(1195);
BFC(1196);
BFC(1197);
BFC(1198);
BFC(1199);
BFC(1200);
BFC(1201);
BFC(1202);
BFC(1203);
BFC(1204);
BFC(1205);
BFC(1206);
BFC(1207);
BFC(1208);
BFC(1209);
BFC(1210);
BFC(1211);
BFC(1212);
BFC(1213);
BFC(1214);
BFC(1215);
BFC(1216);
BFC(1217);
BFC(1218);
BFC(1219);
BFC(1220);
BFC(1221);
BFC(1222);
BFC(1223);
BFC(1224);
BFC(1225);
BFC(1226);
BFC(1227);
BFC(1228);
BFC(1229);
BFC(1230);
BFC(1231);
BFC(1232);
BFC(1233);
BFC(1234);
BFC(1235);
BFC(1236);
BFC(1237);
BFC(1238);
BFC(1239);
BFC(1240);
BFC(1241);
BFC(1242);
BFC(1243);
BFC(1244);
BFC(1245);
BFC(1246);
BFC(1247);
BFC(1248);
BFC(1249);
BFC(1250);
BFC(1251);
BFC(1252);
BFC(1253);
BFC(1254);
BFC(1255);
BFC(1256);
BFC(1257);
BFC(1258);
BFC(1259);
BFC(1260);
BFC(1261);
BFC(1262);
BFC(1263);
BFC(1264);
BFC(1265);
BFC(1266);
BFC(1267);
BFC(1268);
BFC(1269);
BFC(1270);
BFC(1271);
BFC(1272);
BFC(1273);
BFC(1274);
BFC(1275);
BFC(1276);
BFC(1277);
BFC(1278);
BFC(1279);
BFC(1280);
BFC(1281);
BFC(1282);
BFC(1283);
BFC(1284);
BFC(1285);
BFC(1286);
BFC(1287);
BFC(1288);
BFC(1289);
BFC(1290);
BFC(1291);
BFC(1292);
BFC(1293);
BFC(1294);
BFC(1295);
BFC(1296);
BFC(1297);
BFC(1298);
BFC(1299);
BFC(1300);
BFC(1301);
BFC(1302);
BFC(1303);
BFC(1304);
BFC(1305);
BFC(1306);
BFC(1307);
BFC(1308);
BFC(1309);
BFC(1310);
BFC(1311);
BFC(1312);
BFC(1313);
BFC(1314);
BFC(1315);
BFC(1316);
BFC(1317);
BFC(1318);
BFC(1319);
BFC(1320);
BFC(1321);
BFC(1322);
BFC(1323);
BFC(1324);
BFC(1325);
BFC(1326);
BFC(1327);
BFC(1328);
BFC(1329);
BFC(1330);
BFC(1331);
BFC(1332);
BFC(1333);
BFC(1334);
BFC(1335);
BFC(1336);
BFC(1337);
BFC(1338);
BFC(1339);
BFC(1340);
BFC(1341);
BFC(1342);
BFC(1343);
BFC(1344);
BFC(1345);
BFC(1346);
BFC(1347);
BFC(1348);
BFC(1349);
BFC(1350);
BFC(1351);
BFC(1352);
BFC(1353);
BFC(1354);
BFC(1355);
BFC(1356);
BFC(1357);
BFC(1358);
BFC(1359);
BFC(1360);
BFC(1361);
BFC(1362);
BFC(1363);
BFC(1364);
BFC(1365);
BFC(1366);
BFC(1367);
BFC(1368);
BFC(1369);
BFC(1370);
BFC(1371);
BFC(1372);
BFC(1373);
BFC(1374);
BFC(1375);
BFC(1376);
BFC(1377);
BFC(1378);
BFC(1379);
BFC(1380);
BFC(1381);
BFC(1382);
BFC(1383);
BFC(1384);
BFC(1385);
BFC(1386);
BFC(1387);
BFC(1388);
BFC(1389);
BFC(1390);
BFC(1391);
BFC(1392);
BFC(1393);
BFC(1394);
BFC(1395);
BFC(1396);
BFC(1397);
BFC(1398);
BFC(1399);
BFC(1400);
BFC(1401);
BFC(1402);
BFC(1403);
BFC(1404);
BFC(1405);
BFC(1406);
BFC(1407);
BFC(1408);
BFC(1409);
BFC(1410);
BFC(1411);
BFC(1412);
BFC(1413);
BFC(1414);
BFC(1415);
BFC(1416);
BFC(1417);
BFC(1418);
BFC(1419);
BFC(1420);
BFC(1421);
BFC(1422);
BFC(1423);
BFC(1424);
BFC(1425);
BFC(1426);
BFC(1427);
BFC(1428);
BFC(1429);
BFC(1430);
BFC(1431);
BFC(1432);
BFC(1433);
BFC(1434);
BFC(1435);
BFC(1436);
BFC(1437);
BFC(1438);
BFC(1439);
BFC(1440);
BFC(1441);
BFC(1442);
BFC(1443);
BFC(1444);
BFC(1445);
BFC(1446);
BFC(1447);
BFC(1448);
BFC(1449);
BFC(1450);
BFC(1451);
BFC(1452);
BFC(1453);
BFC(1454);
BFC(1455);
BFC(1456);
BFC(1457);
BFC(1458);
BFC(1459);
BFC(1460);
BFC(1461);
BFC(1462);
BFC(1463);
BFC(1464);
BFC(1465);
BFC(1466);
BFC(1467);
BFC(1468);
BFC(1469);
BFC(1470);
BFC(1471);
BFC(1472);
BFC(1473);
BFC(1474);
BFC(1475);
BFC(1476);
BFC(1477);
BFC(1478);
BFC(1479);
BFC(1480);
BFC(1481);
BFC(1482);
BFC(1483);
BFC(1484);
BFC(1485);
BFC(1486);
BFC(1487);
BFC(1488);
BFC(1489);
BFC(1490);
BFC(1491);
BFC(1492);
BFC(1493);
BFC(1494);
BFC(1495);
BFC(1496);
BFC(1497);
BFC(1498);
BFC(1499);
BFC(1500);
BFC(1501);
BFC(1502);
BFC(1503);
BFC(1504);
BFC(1505);
BFC(1506);
BFC(1507);
BFC(1508);
BFC(1509);
BFC(1510);
BFC(1511);
BFC(1512);
BFC(1513);
BFC(1514);
BFC(1515);
BFC(1516);
BFC(1517);
BFC(1518);
BFC(1519);
BFC(1520);
BFC(1521);
BFC(1522);
BFC(1523);
BFC(1524);
BFC(1525);
BFC(1526);
BFC(1527);
BFC(1528);
BFC(1529);
BFC(1530);
BFC(1531);
BFC(1532);
BFC(1533);
BFC(1534);
BFC(1535);
BFC(1536);
BFC(1537);
BFC(1538);
BFC(1539);
BFC(1540);
BFC(1541);
BFC(1542);
BFC(1543);
BFC(1544);
BFC(1545);
BFC(1546);
BFC(1547);
BFC(1548);
BFC(1549);
BFC(1550);
BFC(1551);
BFC(1552);
BFC(1553);
BFC(1554);
BFC(1555);
BFC(1556);
BFC(1557);
BFC(1558);
BFC(1559);
BFC(1560);
BFC(1561);
BFC(1562);
BFC(1563);
BFC(1564);
BFC(1565);
BFC(1566);
BFC(1567);
BFC(1568);
BFC(1569);
BFC(1570);
BFC(1571);
BFC(1572);
BFC(1573);
BFC(1574);
BFC(1575);
BFC(1576);
BFC(1577);
BFC(1578);
BFC(1579);
BFC(1580);
BFC(1581);
BFC(1582);
BFC(1583);
BFC(1584);
BFC(1585);
BFC(1586);
BFC(1587);
BFC(1588);
BFC(1589);
BFC(1590);
BFC(1591);
BFC(1592);
BFC(1593);
BFC(1594);
BFC(1595);
BFC(1596);
BFC(1597);
BFC(1598);
BFC(1599);
BFC(1600);
BFC(1601);
BFC(1602);
BFC(1603);
BFC(1604);
BFC(1605);
BFC(1606);
BFC(1607);
BFC(1608);
BFC(1609);
BFC(1610);
BFC(1611);
BFC(1612);
BFC(1613);
BFC(1614);
BFC(1615);
BFC(1616);
BFC(1617);
BFC(1618);
BFC(1619);
BFC(1620);
BFC(1621);
BFC(1622);
BFC(1623);
BFC(1624);
BFC(1625);
BFC(1626);
BFC(1627);
BFC(1628);
BFC(1629);
BFC(1630);
BFC(1631);
BFC(1632);
BFC(1633);
BFC(1634);
BFC(1635);
BFC(1636);
BFC(1637);
BFC(1638);
BFC(1639);
BFC(1640);
BFC(1641);
BFC(1642);
BFC(1643);
BFC(1644);
BFC(1645);
BFC(1646);
BFC(1647);
BFC(1648);
BFC(1649);
BFC(1650);
BFC(1651);
BFC(1652);
BFC(1653);
BFC(1654);
BFC(1655);
BFC(1656);
BFC(1657);
BFC(1658);
BFC(1659);
BFC(1660);
BFC(1661);
BFC(1662);
BFC(1663);
BFC(1664);
BFC(1665);
BFC(1666);
BFC(1667);
BFC(1668);
BFC(1669);
BFC(1670);
BFC(1671);
BFC(1672);
BFC(1673);
BFC(1674);
BFC(1675);
BFC(1676);
BFC(1677);
BFC(1678);
BFC(1679);
BFC(1680);
BFC(1681);
BFC(1682);
BFC(1683);
BFC(1684);
BFC(1685);
BFC(1686);
BFC(1687);
BFC(1688);
BFC(1689);
BFC(1690);
BFC(1691);
BFC(1692);
BFC(1693);
BFC(1694);
BFC(1695);
BFC(1696);
BFC(1697);
BFC(1698);
BFC(1699);
BFC(1700);
BFC(1701);
BFC(1702);
BFC(1703);
BFC(1704);
BFC(1705);
BFC(1706);
BFC(1707);
BFC(1708);
BFC(1709);
BFC(1710);
BFC(1711);
BFC(1712);
BFC(1713);
BFC(1714);
BFC(1715);
BFC(1716);
BFC(1717);
BFC(1718);
BFC(1719);
BFC(1720);
BFC(1721);
BFC(1722);
BFC(1723);
BFC(1724);
BFC(1725);
BFC(1726);
BFC(1727);
BFC(1728);
BFC(1729);
BFC(1730);
BFC(1731);
BFC(1732);
BFC(1733);
BFC(1734);
BFC(1735);
BFC(1736);
BFC(1737);
BFC(1738);
BFC(1739);
BFC(1740);
BFC(1741);
BFC(1742);
BFC(1743);
BFC(1744);
BFC(1745);
BFC(1746);
BFC(1747);
BFC(1748);
BFC(1749);
BFC(1750);
BFC(1751);
BFC(1752);
BFC(1753);
BFC(1754);
BFC(1755);
BFC(1756);
BFC(1757);
BFC(1758);
BFC(1759);
BFC(1760);
BFC(1761);
BFC(1762);
BFC(1763);
BFC(1764);
BFC(1765);
BFC(1766);
BFC(1767);
BFC(1768);
BFC(1769);
BFC(1770);
BFC(1771);
BFC(1772);
BFC(1773);
BFC(1774);
BFC(1775);
BFC(1776);
BFC(1777);
BFC(1778);
BFC(1779);
BFC(1780);
BFC(1781);
BFC(1782);
BFC(1783);
BFC(1784);
BFC(1785);
BFC(1786);
BFC(1787);
BFC(1788);
BFC(1789);
BFC(1790);
BFC(1791);
BFC(1792);
BFC(1793);
BFC(1794);
BFC(1795);
BFC(1796);
BFC(1797);
BFC(1798);
BFC(1799);
BFC(1800);
BFC(1801);
BFC(1802);
BFC(1803);
BFC(1804);
BFC(1805);
BFC(1806);
BFC(1807);
BFC(1808);
BFC(1809);
BFC(1810);
BFC(1811);
BFC(1812);
BFC(1813);
BFC(1814);
BFC(1815);
BFC(1816);
BFC(1817);
BFC(1818);
BFC(1819);
BFC(1820);
BFC(1821);
BFC(1822);
BFC(1823);
BFC(1824);
BFC(1825);
BFC(1826);
BFC(1827);
BFC(1828);
BFC(1829);
BFC(1830);
BFC(1831);
BFC(1832);
BFC(1833);
BFC(1834);
BFC(1835);
BFC(1836);
BFC(1837);
BFC(1838);
BFC(1839);
BFC(1840);
BFC(1841);
BFC(1842);
BFC(1843);
BFC(1844);
BFC(1845);
BFC(1846);
BFC(1847);
BFC(1848);
BFC(1849);
BFC(1850);
BFC(1851);
BFC(1852);
BFC(1853);
BFC(1854);
BFC(1855);
BFC(1856);
BFC(1857);
BFC(1858);
BFC(1859);
BFC(1860);
BFC(1861);
BFC(1862);
BFC(1863);
BFC(1864);
BFC(1865);
BFC(1866);
BFC(1867);
BFC(1868);
BFC(1869);
BFC(1870);
BFC(1871);
BFC(1872);
BFC(1873);
BFC(1874);
BFC(1875);
BFC(1876);
BFC(1877);
BFC(1878);
BFC(1879);
BFC(1880);
BFC(1881);
BFC(1882);
BFC(1883);
BFC(1884);
BFC(1885);
BFC(1886);
BFC(1887);
BFC(1888);
BFC(1889);
BFC(1890);
BFC(1891);
BFC(1892);
BFC(1893);
BFC(1894);
BFC(1895);
BFC(1896);
BFC(1897);
BFC(1898);
BFC(1899);
BFC(1900);
BFC(1901);
BFC(1902);
BFC(1903);
BFC(1904);
BFC(1905);
BFC(1906);
BFC(1907);
BFC(1908);
BFC(1909);
BFC(1910);
BFC(1911);
BFC(1912);
BFC(1913);
BFC(1914);
BFC(1915);
BFC(1916);
BFC(1917);
BFC(1918);
BFC(1919);
BFC(1920);
BFC(1921);
BFC(1922);
BFC(1923);
BFC(1924);
BFC(1925);
BFC(1926);
BFC(1927);
BFC(1928);
BFC(1929);
BFC(1930);
BFC(1931);
BFC(1932);
BFC(1933);
BFC(1934);
BFC(1935);
BFC(1936);
BFC(1937);
BFC(1938);
BFC(1939);
BFC(1940);
BFC(1941);
BFC(1942);
BFC(1943);
BFC(1944);
BFC(1945);
BFC(1946);
BFC(1947);
BFC(1948);
BFC(1949);
BFC(1950);
BFC(1951);
BFC(1952);
BFC(1953);
BFC(1954);
BFC(1955);
BFC(1956);
BFC(1957);
BFC(1958);
BFC(1959);
BFC(1960);
BFC(1961);
BFC(1962);
BFC(1963);
BFC(1964);
BFC(1965);
BFC(1966);
BFC(1967);
BFC(1968);
BFC(1969);
BFC(1970);
BFC(1971);
BFC(1972);
BFC(1973);
BFC(1974);
BFC(1975);
BFC(1976);
BFC(1977);
BFC(1978);
BFC(1979);
BFC(1980);
BFC(1981);
BFC(1982);
BFC(1983);
BFC(1984);
BFC(1985);
BFC(1986);
BFC(1987);
BFC(1988);
BFC(1989);
BFC(1990);
BFC(1991);
BFC(1992);
BFC(1993);
BFC(1994);
BFC(1995);
BFC(1996);
BFC(1997);
BFC(1998);
BFC(1999);
BFC(2000);
BFC(2001);
BFC(2002);
BFC(2003);
BFC(2004);
BFC(2005);
BFC(2006);
BFC(2007);
BFC(2008);
BFC(2009);
BFC(2010);
BFC(2011);
BFC(2012);
BFC(2013);
BFC(2014);
BFC(2015);
BFC(2016);
BFC(2017);
BFC(2018);
BFC(2019);
BFC(2020);
BFC(2021);
BFC(2022);
BFC(2023);
BFC(2024);
BFC(2025);
BFC(2026);
BFC(2027);
BFC(2028);
BFC(2029);
BFC(2030);
BFC(2031);
BFC(2032);
BFC(2033);
BFC(2034);
BFC(2035);
BFC(2036);
BFC(2037);
BFC(2038);
BFC(2039);
BFC(2040);
BFC(2041);
BFC(2042);
BFC(2043);
BFC(2044);
BFC(2045);
BFC(2046);
BFC(2047);
BFC(2048);
BFC(2049);
BFC(2050);
BFC(2051);
BFC(2052);
BFC(2053);
BFC(2054);
BFC(2055);
BFC(2056);
BFC(2057);
BFC(2058);
BFC(2059);
BFC(2060);
BFC(2061);
BFC(2062);
BFC(2063);
BFC(2064);
BFC(2065);
BFC(2066);
BFC(2067);
BFC(2068);
BFC(2069);
BFC(2070);
BFC(2071);
BFC(2072);
BFC(2073);
BFC(2074);
BFC(2075);
BFC(2076);
BFC(2077);
BFC(2078);
BFC(2079);
BFC(2080);
BFC(2081);
BFC(2082);
BFC(2083);
BFC(2084);
BFC(2085);
BFC(2086);
BFC(2087);
BFC(2088);
BFC(2089);
BFC(2090);
BFC(2091);
BFC(2092);
BFC(2093);
BFC(2094);
BFC(2095);
BFC(2096);
BFC(2097);
BFC(2098);
BFC(2099);
BFC(2100);
BFC(2101);
BFC(2102);
BFC(2103);
BFC(2104);
BFC(2105);
BFC(2106);
BFC(2107);
BFC(2108);
BFC(2109);
BFC(2110);
BFC(2111);
BFC(2112);
BFC(2113);
BFC(2114);
BFC(2115);
BFC(2116);
BFC(2117);
BFC(2118);
BFC(2119);
BFC(2120);
BFC(2121);
BFC(2122);
BFC(2123);
BFC(2124);
BFC(2125);
BFC(2126);
BFC(2127);
BFC(2128);
BFC(2129);
BFC(2130);
BFC(2131);
BFC(2132);
BFC(2133);
BFC(2134);
BFC(2135);
BFC(2136);
BFC(2137);
BFC(2138);
BFC(2139);
BFC(2140);
BFC(2141);
BFC(2142);
BFC(2143);
BFC(2144);
BFC(2145);
BFC(2146);
BFC(2147);
BFC(2148);
BFC(2149);
BFC(2150);
BFC(2151);
BFC(2152);
BFC(2153);
BFC(2154);
BFC(2155);
BFC(2156);
BFC(2157);
BFC(2158);
BFC(2159);
BFC(2160);
BFC(2161);
BFC(2162);
BFC(2163);
BFC(2164);
BFC(2165);
BFC(2166);
BFC(2167);
BFC(2168);
BFC(2169);
BFC(2170);
BFC(2171);
BFC(2172);
BFC(2173);
BFC(2174);
BFC(2175);
BFC(2176);
BFC(2177);
BFC(2178);
BFC(2179);
BFC(2180);
BFC(2181);
BFC(2182);
BFC(2183);
BFC(2184);
BFC(2185);
BFC(2186);
BFC(2187);
BFC(2188);
BFC(2189);
BFC(2190);
BFC(2191);
BFC(2192);
BFC(2193);
BFC(2194);
BFC(2195);
BFC(2196);
BFC(2197);
BFC(2198);
BFC(2199);
BFC(2200);
BFC(2201);
BFC(2202);
BFC(2203);
BFC(2204);
BFC(2205);
BFC(2206);
BFC(2207);
BFC(2208);
BFC(2209);
BFC(2210);
BFC(2211);
BFC(2212);
BFC(2213);
BFC(2214);
BFC(2215);
BFC(2216);
BFC(2217);
BFC(2218);
BFC(2219);
BFC(2220);
BFC(2221);
BFC(2222);
BFC(2223);
BFC(2224);
BFC(2225);
BFC(2226);
BFC(2227);
BFC(2228);
BFC(2229);
BFC(2230);
BFC(2231);
BFC(2232);
BFC(2233);
BFC(2234);
BFC(2235);
BFC(2236);
BFC(2237);
BFC(2238);
BFC(2239);
BFC(2240);
BFC(2241);
BFC(2242);
BFC(2243);
BFC(2244);
BFC(2245);
BFC(2246);
BFC(2247);
BFC(2248);
BFC(2249);
BFC(2250);
BFC(2251);
BFC(2252);
BFC(2253);
BFC(2254);
BFC(2255);
BFC(2256);
BFC(2257);
BFC(2258);
BFC(2259);
BFC(2260);
BFC(2261);
BFC(2262);
BFC(2263);
BFC(2264);
BFC(2265);
BFC(2266);
BFC(2267);
BFC(2268);
BFC(2269);
BFC(2270);
BFC(2271);
BFC(2272);
BFC(2273);
BFC(2274);
BFC(2275);
BFC(2276);
BFC(2277);
BFC(2278);
BFC(2279);
BFC(2280);
BFC(2281);
BFC(2282);
BFC(2283);
BFC(2284);
BFC(2285);
BFC(2286);
BFC(2287);
BFC(2288);
BFC(2289);
BFC(2290);
BFC(2291);
BFC(2292);
BFC(2293);
BFC(2294);
BFC(2295);
BFC(2296);
BFC(2297);
BFC(2298);
BFC(2299);
BFC(2300);
BFC(2301);
BFC(2302);
BFC(2303);
BFC(2304);
BFC(2305);
BFC(2306);
BFC(2307);
BFC(2308);
BFC(2309);
BFC(2310);
BFC(2311);
BFC(2312);
BFC(2313);
BFC(2314);
BFC(2315);
BFC(2316);
BFC(2317);
BFC(2318);
BFC(2319);
BFC(2320);
BFC(2321);
BFC(2322);
BFC(2323);
BFC(2324);
BFC(2325);
BFC(2326);
BFC(2327);
BFC(2328);
BFC(2329);
BFC(2330);
BFC(2331);
BFC(2332);
BFC(2333);
BFC(2334);
BFC(2335);
BFC(2336);
BFC(2337);
BFC(2338);
BFC(2339);
BFC(2340);
BFC(2341);
BFC(2342);
BFC(2343);
BFC(2344);
BFC(2345);
BFC(2346);
BFC(2347);
BFC(2348);
BFC(2349);
BFC(2350);
BFC(2351);
BFC(2352);
BFC(2353);
BFC(2354);
BFC(2355);
BFC(2356);
BFC(2357);
BFC(2358);
BFC(2359);
BFC(2360);
BFC(2361);
BFC(2362);
BFC(2363);
BFC(2364);
BFC(2365);
BFC(2366);
BFC(2367);
BFC(2368);
BFC(2369);
BFC(2370);
BFC(2371);
BFC(2372);
BFC(2373);
BFC(2374);
BFC(2375);
BFC(2376);
BFC(2377);
BFC(2378);
BFC(2379);
BFC(2380);
BFC(2381);
BFC(2382);
BFC(2383);
BFC(2384);
BFC(2385);
BFC(2386);
BFC(2387);
BFC(2388);
BFC(2389);
BFC(2390);
BFC(2391);
BFC(2392);
BFC(2393);
BFC(2394);
BFC(2395);
BFC(2396);
BFC(2397);
BFC(2398);
BFC(2399);
BFC(2400);
BFC(2401);
BFC(2402);
BFC(2403);
BFC(2404);
BFC(2405);
BFC(2406);
BFC(2407);
BFC(2408);
BFC(2409);
BFC(2410);
BFC(2411);
BFC(2412);
BFC(2413);
BFC(2414);
BFC(2415);
BFC(2416);
BFC(2417);
BFC(2418);
BFC(2419);
BFC(2420);
BFC(2421);
BFC(2422);
BFC(2423);
BFC(2424);
BFC(2425);
BFC(2426);
BFC(2427);
BFC(2428);
BFC(2429);
BFC(2430);
BFC(2431);
BFC(2432);
BFC(2433);
BFC(2434);
BFC(2435);
BFC(2436);
BFC(2437);
BFC(2438);
BFC(2439);
BFC(2440);
BFC(2441);
BFC(2442);
BFC(2443);
BFC(2444);
BFC(2445);
BFC(2446);
BFC(2447);
BFC(2448);
BFC(2449);
BFC(2450);
BFC(2451);
BFC(2452);
BFC(2453);
BFC(2454);
BFC(2455);
BFC(2456);
BFC(2457);
BFC(2458);
BFC(2459);
BFC(2460);
BFC(2461);
BFC(2462);
BFC(2463);
BFC(2464);
BFC(2465);
BFC(2466);
BFC(2467);
BFC(2468);
BFC(2469);
BFC(2470);
BFC(2471);
BFC(2472);
BFC(2473);
BFC(2474);
BFC(2475);
BFC(2476);
BFC(2477);
BFC(2478);
BFC(2479);
BFC(2480);
BFC(2481);
BFC(2482);
BFC(2483);
BFC(2484);
BFC(2485);
BFC(2486);
BFC(2487);
BFC(2488);
BFC(2489);
BFC(2490);
BFC(2491);
BFC(2492);
BFC(2493);
BFC(2494);
BFC(2495);
BFC(2496);
BFC(2497);
BFC(2498);
BFC(2499);
BFC(2500);
BFC(2501);
BFC(2502);
BFC(2503);
BFC(2504);
BFC(2505);
BFC(2506);
BFC(2507);
BFC(2508);
BFC(2509);
BFC(2510);
BFC(2511);
BFC(2512);
BFC(2513);
BFC(2514);
BFC(2515);
BFC(2516);
BFC(2517);
BFC(2518);
BFC(2519);
BFC(2520);
BFC(2521);
BFC(2522);
BFC(2523);
BFC(2524);
BFC(2525);
BFC(2526);
BFC(2527);
BFC(2528);
BFC(2529);
BFC(2530);
BFC(2531);
BFC(2532);
BFC(2533);
BFC(2534);
BFC(2535);
BFC(2536);
BFC(2537);
BFC(2538);
BFC(2539);
BFC(2540);
BFC(2541);
BFC(2542);
BFC(2543);
BFC(2544);
BFC(2545);
BFC(2546);
BFC(2547);
BFC(2548);
BFC(2549);
BFC(2550);
BFC(2551);
BFC(2552);
BFC(2553);
BFC(2554);
BFC(2555);
BFC(2556);
BFC(2557);
BFC(2558);
BFC(2559);
BFC(2560);
BFC(2561);
BFC(2562);
BFC(2563);
BFC(2564);
BFC(2565);
BFC(2566);
BFC(2567);
BFC(2568);
BFC(2569);
BFC(2570);
BFC(2571);
BFC(2572);
BFC(2573);
BFC(2574);
BFC(2575);
BFC(2576);
BFC(2577);
BFC(2578);
BFC(2579);
BFC(2580);
BFC(2581);
BFC(2582);
BFC(2583);
BFC(2584);
BFC(2585);
BFC(2586);
BFC(2587);
BFC(2588);
BFC(2589);
BFC(2590);
BFC(2591);
BFC(2592);
BFC(2593);
BFC(2594);
BFC(2595);
BFC(2596);
BFC(2597);
BFC(2598);
BFC(2599);
BFC(2600);
BFC(2601);
BFC(2602);
BFC(2603);
BFC(2604);
BFC(2605);
BFC(2606);
BFC(2607);
BFC(2608);
BFC(2609);
BFC(2610);
BFC(2611);
BFC(2612);
BFC(2613);
BFC(2614);
BFC(2615);
BFC(2616);
BFC(2617);
BFC(2618);
BFC(2619);
BFC(2620);
BFC(2621);
BFC(2622);
BFC(2623);
BFC(2624);
BFC(2625);
BFC(2626);
BFC(2627);
BFC(2628);
BFC(2629);
BFC(2630);
BFC(2631);
BFC(2632);
BFC(2633);
BFC(2634);
BFC(2635);
BFC(2636);
BFC(2637);
BFC(2638);
BFC(2639);
BFC(2640);
BFC(2641);
BFC(2642);
BFC(2643);
BFC(2644);
BFC(2645);
BFC(2646);
BFC(2647);
BFC(2648);
BFC(2649);
BFC(2650);
BFC(2651);
BFC(2652);
BFC(2653);
BFC(2654);
BFC(2655);
BFC(2656);
BFC(2657);
BFC(2658);
BFC(2659);
BFC(2660);
BFC(2661);
BFC(2662);
BFC(2663);
BFC(2664);
BFC(2665);
BFC(2666);
BFC(2667);
BFC(2668);
BFC(2669);
BFC(2670);
BFC(2671);
BFC(2672);
BFC(2673);
BFC(2674);
BFC(2675);
BFC(2676);
BFC(2677);
BFC(2678);
BFC(2679);
BFC(2680);
BFC(2681);
BFC(2682);
BFC(2683);
BFC(2684);
BFC(2685);
BFC(2686);
BFC(2687);
BFC(2688);
BFC(2689);
BFC(2690);
BFC(2691);
BFC(2692);
BFC(2693);
BFC(2694);
BFC(2695);
BFC(2696);
BFC(2697);
BFC(2698);
BFC(2699);
BFC(2700);
BFC(2701);
BFC(2702);
BFC(2703);
BFC(2704);
BFC(2705);
BFC(2706);
BFC(2707);
BFC(2708);
BFC(2709);
BFC(2710);
BFC(2711);
BFC(2712);
BFC(2713);
BFC(2714);
BFC(2715);
BFC(2716);
BFC(2717);
BFC(2718);
BFC(2719);
BFC(2720);
BFC(2721);
BFC(2722);
BFC(2723);
BFC(2724);
BFC(2725);
BFC(2726);
BFC(2727);
BFC(2728);
BFC(2729);
BFC(2730);
BFC(2731);
BFC(2732);
BFC(2733);
BFC(2734);
BFC(2735);
BFC(2736);
BFC(2737);
BFC(2738);
BFC(2739);
BFC(2740);
BFC(2741);
BFC(2742);
BFC(2743);
BFC(2744);
BFC(2745);
BFC(2746);
BFC(2747);
BFC(2748);
BFC(2749);
BFC(2750);
BFC(2751);
BFC(2752);
BFC(2753);
BFC(2754);
BFC(2755);
BFC(2756);
BFC(2757);
BFC(2758);
BFC(2759);
BFC(2760);
BFC(2761);
BFC(2762);
BFC(2763);
BFC(2764);
BFC(2765);
BFC(2766);
BFC(2767);
BFC(2768);
BFC(2769);
BFC(2770);
BFC(2771);
BFC(2772);
BFC(2773);
BFC(2774);
BFC(2775);
BFC(2776);
BFC(2777);
BFC(2778);
BFC(2779);
BFC(2780);
BFC(2781);
BFC(2782);
BFC(2783);
BFC(2784);
BFC(2785);
BFC(2786);
BFC(2787);
BFC(2788);
BFC(2789);
BFC(2790);
BFC(2791);
BFC(2792);
BFC(2793);
BFC(2794);
BFC(2795);
BFC(2796);
BFC(2797);
BFC(2798);
BFC(2799);
BFC(2800);
BFC(2801);
BFC(2802);
BFC(2803);
BFC(2804);
BFC(2805);
BFC(2806);
BFC(2807);
BFC(2808);
BFC(2809);
BFC(2810);
BFC(2811);
BFC(2812);
BFC(2813);
BFC(2814);
BFC(2815);
BFC(2816);
BFC(2817);
BFC(2818);
BFC(2819);
BFC(2820);
BFC(2821);
BFC(2822);
BFC(2823);
BFC(2824);
BFC(2825);
BFC(2826);
BFC(2827);
BFC(2828);
BFC(2829);
BFC(2830);
BFC(2831);
BFC(2832);
BFC(2833);
BFC(2834);
BFC(2835);
BFC(2836);
BFC(2837);
BFC(2838);
BFC(2839);
BFC(2840);
BFC(2841);
BFC(2842);
BFC(2843);
BFC(2844);
BFC(2845);
BFC(2846);
BFC(2847);
BFC(2848);
BFC(2849);
BFC(2850);
BFC(2851);
BFC(2852);
BFC(2853);
BFC(2854);
BFC(2855);
BFC(2856);
BFC(2857);
BFC(2858);
BFC(2859);
BFC(2860);
BFC(2861);
BFC(2862);
BFC(2863);
BFC(2864);
BFC(2865);
BFC(2866);
BFC(2867);
BFC(2868);
BFC(2869);
BFC(2870);
BFC(2871);
BFC(2872);
BFC(2873);
BFC(2874);
BFC(2875);
BFC(2876);
BFC(2877);
BFC(2878);
BFC(2879);
BFC(2880);
BFC(2881);
BFC(2882);
BFC(2883);
BFC(2884);
BFC(2885);
BFC(2886);
BFC(2887);
BFC(2888);
BFC(2889);
BFC(2890);
BFC(2891);
BFC(2892);
BFC(2893);
BFC(2894);
BFC(2895);
BFC(2896);
BFC(2897);
BFC(2898);
BFC(2899);
BFC(2900);
BFC(2901);
BFC(2902);
BFC(2903);
BFC(2904);
BFC(2905);
BFC(2906);
BFC(2907);
BFC(2908);
BFC(2909);
BFC(2910);
BFC(2911);
BFC(2912);
BFC(2913);
BFC(2914);
BFC(2915);
BFC(2916);
BFC(2917);
BFC(2918);
BFC(2919);
BFC(2920);
BFC(2921);
BFC(2922);
BFC(2923);
BFC(2924);
BFC(2925);
BFC(2926);
BFC(2927);
BFC(2928);
BFC(2929);
BFC(2930);
BFC(2931);
BFC(2932);
BFC(2933);
BFC(2934);
BFC(2935);
BFC(2936);
BFC(2937);
BFC(2938);
BFC(2939);
BFC(2940);
BFC(2941);
BFC(2942);
BFC(2943);
BFC(2944);
BFC(2945);
BFC(2946);
BFC(2947);
BFC(2948);
BFC(2949);
BFC(2950);
BFC(2951);
BFC(2952);
BFC(2953);
BFC(2954);
BFC(2955);
BFC(2956);
BFC(2957);
BFC(2958);
BFC(2959);
BFC(2960);
BFC(2961);
BFC(2962);
BFC(2963);
BFC(2964);
BFC(2965);
BFC(2966);
BFC(2967);
BFC(2968);
BFC(2969);
BFC(2970);
BFC(2971);
BFC(2972);
BFC(2973);
BFC(2974);
BFC(2975);
BFC(2976);
BFC(2977);
BFC(2978);
BFC(2979);
BFC(2980);
BFC(2981);
BFC(2982);
BFC(2983);
BFC(2984);
BFC(2985);
BFC(2986);
BFC(2987);
BFC(2988);
BFC(2989);
BFC(2990);
BFC(2991);
BFC(2992);
BFC(2993);
BFC(2994);
BFC(2995);
BFC(2996);
BFC(2997);
BFC(2998);
BFC(2999);
BFC(3000);
BFC(3001);
BFC(3002);
BFC(3003);
BFC(3004);
BFC(3005);
BFC(3006);
BFC(3007);
BFC(3008);
BFC(3009);
BFC(3010);
BFC(3011);
BFC(3012);
BFC(3013);
BFC(3014);
BFC(3015);
BFC(3016);
BFC(3017);
BFC(3018);
BFC(3019);
BFC(3020);
BFC(3021);
BFC(3022);
BFC(3023);
BFC(3024);
BFC(3025);
BFC(3026);
BFC(3027);
BFC(3028);
BFC(3029);
BFC(3030);
BFC(3031);
BFC(3032);
BFC(3033);
BFC(3034);
BFC(3035);
BFC(3036);
BFC(3037);
BFC(3038);
BFC(3039);
BFC(3040);
BFC(3041);
BFC(3042);
BFC(3043);
BFC(3044);
BFC(3045);
BFC(3046);
BFC(3047);


