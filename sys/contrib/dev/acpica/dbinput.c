/*******************************************************************************
 *
 * Module Name: dbinput - user front-end to the AML debugger
 *              $Revision: 52 $
 *
 ******************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999, Intel Corp.  All rights
 * reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights.  You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code.  No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision.  In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change.  Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee.  Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution.  In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE.  ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT,  ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES.  INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS.  INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES.  THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government.  In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************/


#include "acpi.h"
#include "acparser.h"
#include "actables.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "acdebug.h"


#ifdef ENABLE_DEBUGGER

#define _COMPONENT          DEBUGGER
        MODULE_NAME         ("dbinput")


/*
 * Globals that are specific to the debugger
 */

NATIVE_CHAR             LineBuf[80];
NATIVE_CHAR             ParsedBuf[80];
NATIVE_CHAR             ScopeBuf[40];
NATIVE_CHAR             DebugFilename[40];
NATIVE_CHAR             *Args[DB_MAX_ARGS];
NATIVE_CHAR             *Buffer;
NATIVE_CHAR             *Filename = NULL;
BOOLEAN                 OutputToFile = FALSE;


UINT32                  AcpiGbl_DbDebugLevel = 0x0FFFFFFF;
UINT32                  AcpiGbl_DbConsoleDebugLevel = NORMAL_DEFAULT | TRACE_TABLES;
UINT8                   AcpiGbl_DbOutputFlags = DB_CONSOLE_OUTPUT;


BOOLEAN                 opt_tables      = FALSE;
BOOLEAN                 opt_disasm      = FALSE;
BOOLEAN                 opt_stats       = FALSE;
BOOLEAN                 opt_parse_jit   = FALSE;
BOOLEAN                 opt_verbose     = TRUE;


/*
 * Top-level debugger commands.
 *
 * This list of commands must match the string table below it
 */

enum AcpiAmlDebuggerCommands
{
    CMD_NOT_FOUND = 0,
    CMD_NULL,
    CMD_ALLOCATIONS,
    CMD_ARGS,
    CMD_ARGUMENTS,
    CMD_BREAKPOINT,
    CMD_CALL,
    CMD_CLOSE,
    CMD_DEBUG,
    CMD_DUMP,
    CMD_ENABLEACPI,
    CMD_EVENT,
    CMD_EXECUTE,
    CMD_EXIT,
    CMD_FIND,
    CMD_GO,
    CMD_HELP,
    CMD_HELP2,
    CMD_HISTORY,
    CMD_HISTORY_EXE,
    CMD_HISTORY_LAST,
    CMD_INFORMATION,
    CMD_INTO,
    CMD_LEVEL,
    CMD_LIST,
    CMD_LOAD,
    CMD_LOCALS,
    CMD_METHODS,
    CMD_NAMESPACE,
    CMD_NOTIFY,
    CMD_OBJECT,
    CMD_OPEN,
    CMD_OWNER,
    CMD_PREFIX,
    CMD_QUIT,
    CMD_REFERENCES,
    CMD_RESULTS,
    CMD_SET,
    CMD_STATS,
    CMD_STOP,
    CMD_TABLES,
    CMD_TERMINATE,
    CMD_THREADS,
    CMD_TREE,
    CMD_UNLOAD
};

#define CMD_FIRST_VALID     2


COMMAND_INFO                Commands[] =
{
    {"<NOT FOUND>",  0},
    {"<NULL>",       0},
    {"ALLOCATIONS",  0},
    {"ARGS",         0},
    {"ARGUMENTS",    0},
    {"BREAKPOINT",   1},
    {"CALL",         0},
    {"CLOSE",        0},
    {"DEBUG",        1},
    {"DUMP",         1},
    {"ENABLEACPI",   0},
    {"EVENT",        1},
    {"EXECUTE",      1},
    {"EXIT",         0},
    {"FIND",         1},
    {"GO",           0},
    {"HELP",         0},
    {"?",            0},
    {"HISTORY",      0},
    {"!",            1},
    {"!!",           0},
    {"INFORMATION",  0},
    {"INTO",         0},
    {"LEVEL",        0},
    {"LIST",         0},
    {"LOAD",         1},
    {"LOCALS",       0},
    {"METHODS",      0},
    {"NAMESPACE",    0},
    {"NOTIFY",       2},
    {"OBJECT",       1},
    {"OPEN",         1},
    {"OWNER",        1},
    {"PREFIX",       0},
    {"QUIT",         0},
    {"REFERENCES",   1},
    {"RESULTS",      0},
    {"SET",          3},
    {"STATS",        0},
    {"STOP",         0},
    {"TABLES",       0},
    {"TERMINATE",    0},
    {"THREADS",      3},
    {"TREE",         0},
    {"UNLOAD",       0},
    {NULL,           0}
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayHelp
 *
 * PARAMETERS:  HelpType        - Subcommand (optional)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print a usage message.
 *
 ******************************************************************************/

void
AcpiDbDisplayHelp (
    NATIVE_CHAR             *HelpType)
{


    /* No parameter, just give the overview */

    if (!HelpType)
    {
        AcpiOsPrintf ("ACPI CA Debugger Commands\n\n");
        AcpiOsPrintf ("The following classes of commands are available.  Help is available for\n");
        AcpiOsPrintf ("each class by entering \"Help <ClassName>\"\n\n");
        AcpiOsPrintf ("    [GENERAL]       General-Purpose Commands\n");
        AcpiOsPrintf ("    [NAMESPACE]     Namespace Access Commands\n");
        AcpiOsPrintf ("    [METHOD]        Control Method Execution Commands\n");
        AcpiOsPrintf ("    [FILE]          File I/O Commands\n");
        return;

    }


    /*
     * Parameter is the command class
     *
     * The idea here is to keep each class of commands smaller than a screenful
     */

    switch (HelpType[0])
    {
    case 'G':
        AcpiOsPrintf ("\nGeneral-Purpose Commands\n\n");
        AcpiOsPrintf ("Allocations                         Display list of current memory allocations\n");
        AcpiOsPrintf ("Dump <Address>|<Namepath>\n");
        AcpiOsPrintf ("     [Byte|Word|Dword|Qword]        Display ACPI objects or memory\n");
        AcpiOsPrintf ("EnableAcpi                          Enable ACPI (hardware) mode\n");
        AcpiOsPrintf ("Help                                This help screen\n");
        AcpiOsPrintf ("History                             Display command history buffer\n");
        AcpiOsPrintf ("Level [<DebugLevel>] [console]      Get/Set debug level for file or console\n");
        AcpiOsPrintf ("Quit or Exit                        Exit this command\n");
        AcpiOsPrintf ("Stats [Allocations|Memory|Misc\n");
        AcpiOsPrintf ("       |Objects|Tables]             Display namespace and memory statistics\n");
        AcpiOsPrintf ("Tables                              Display info about loaded ACPI tables\n");
        AcpiOsPrintf ("Unload                              Unload an ACPI table\n");
        AcpiOsPrintf ("! <CommandNumber>                   Execute command from history buffer\n");
        AcpiOsPrintf ("!!                                  Execute last command again\n");
        return;

    case 'N':
        AcpiOsPrintf ("\nNamespace Access Commands\n\n");
        AcpiOsPrintf ("Debug <Namepath> [Arguments]        Single Step a control method\n");
        AcpiOsPrintf ("Event <F|G> <Value>                 Generate AcpiEvent (Fixed/GPE)\n");
        AcpiOsPrintf ("Execute <Namepath> [Arguments]      Execute control method\n");
        AcpiOsPrintf ("Find <Name>   (? is wildcard)       Find ACPI name(s) with wildcards\n");
        AcpiOsPrintf ("Method                              Display list of loaded control methods\n");
        AcpiOsPrintf ("Namespace [<Addr>|<Path>] [Depth]   Display loaded namespace tree/subtree\n");
        AcpiOsPrintf ("Notify <NamePath> <Value>           Send a notification\n");
        AcpiOsPrintf ("Objects <ObjectType>                Display all objects of the given type\n");
        AcpiOsPrintf ("Owner <OwnerId> [Depth]             Display loaded namespace by object owner\n");
        AcpiOsPrintf ("Prefix [<NamePath>]                 Set or Get current execution prefix\n");
        AcpiOsPrintf ("References <Addr>                   Find all references to object at addr\n");
        AcpiOsPrintf ("Terminate                           Delete namespace and all internal objects\n");
        AcpiOsPrintf ("Thread <Threads><Loops><NamePath>   Spawn threads to execute method(s)\n");
        return;

    case 'M':
        AcpiOsPrintf ("\nControl Method Execution Commands\n\n");
        AcpiOsPrintf ("Arguments (or Args)                 Display method arguments\n");
        AcpiOsPrintf ("Breakpoint <AmlOffset>              Set an AML execution breakpoint\n");
        AcpiOsPrintf ("Call                                Run to next control method invocation\n");
        AcpiOsPrintf ("Go                                  Allow method to run to completion\n");
        AcpiOsPrintf ("Information                         Display info about the current method\n");
        AcpiOsPrintf ("Into                                Step into (not over) a method call\n");
        AcpiOsPrintf ("List [# of AcpiAml Opcodes]             Display method ASL statements\n");
        AcpiOsPrintf ("Locals                              Display method local variables\n");
        AcpiOsPrintf ("Results                             Display method result stack\n");
        AcpiOsPrintf ("Set <A|L> <#> <Value>               Set method data (Arguments/Locals)\n");
        AcpiOsPrintf ("Stop                                Terminate control method\n");
        AcpiOsPrintf ("Tree                                Display control method calling tree\n");
        AcpiOsPrintf ("<Enter>                             Single step next AML opcode (over calls)\n");
        return;

    case 'F':
        AcpiOsPrintf ("\nFile I/O Commands\n\n");
        AcpiOsPrintf ("Close                               Close debug output file\n");
        AcpiOsPrintf ("Open <Output Filename>              Open a file for debug output\n");
        AcpiOsPrintf ("Load <Input Filename>               Load ACPI table from a file\n");
        return;

    default:
        AcpiOsPrintf ("Unrecognized Command Class: %x\n", HelpType);
        return;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbGetNextToken
 *
 * PARAMETERS:  String          - Command buffer
 *              Next            - Return value, end of next token
 *
 * RETURN:      Pointer to the start of the next token.
 *
 * DESCRIPTION: Command line parsing.  Get the next token on the command line
 *
 ******************************************************************************/

NATIVE_CHAR *
AcpiDbGetNextToken (
    NATIVE_CHAR             *String,
    NATIVE_CHAR             **Next)
{
    NATIVE_CHAR             *Start;

    /* At end of buffer? */

    if (!String || !(*String))
    {
        return (NULL);
    }


    /* Get rid of any spaces at the beginning */

    if (*String == ' ')
    {
        while (*String && (*String == ' '))
        {
            String++;
        }

        if (!(*String))
        {
            return (NULL);
        }
    }

    Start = String;

    /* Find end of token */

    while (*String && (*String != ' '))
    {
        String++;
    }


    if (!(*String))
    {
        *Next = NULL;
    }

    else
    {
        *String = 0;
        *Next = String + 1;
    }

    return (Start);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbGetLine
 *
 * PARAMETERS:  InputBuffer         - Command line buffer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get the next command line from the user.  Gets entire line
 *              up to the next newline
 *
 ******************************************************************************/

UINT32
AcpiDbGetLine (
    NATIVE_CHAR             *InputBuffer)
{
    UINT32                  i;
    UINT32                  Count;
    NATIVE_CHAR             *Next;
    NATIVE_CHAR             *This;


    STRCPY (ParsedBuf, InputBuffer);
    STRUPR (ParsedBuf);

    This = ParsedBuf;
    for (i = 0; i < DB_MAX_ARGS; i++)
    {
        Args[i] = AcpiDbGetNextToken (This, &Next);
        if (!Args[i])
        {
            break;
        }

        This = Next;
    }


    /* Uppercase the actual command */

    if (Args[0])
    {
        STRUPR (Args[0]);
    }

    Count = i;
    if (Count)
    {
        Count--;  /* Number of args only */
    }

    return (Count);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbMatchCommand
 *
 * PARAMETERS:  UserCommand             - User command line
 *
 * RETURN:      Index into command array, -1 if not found
 *
 * DESCRIPTION: Search command array for a command match
 *
 ******************************************************************************/

UINT32
AcpiDbMatchCommand (
    NATIVE_CHAR             *UserCommand)
{
    UINT32                  i;


    if (!UserCommand || UserCommand[0] == 0)
    {
        return (CMD_NULL);
    }

    for (i = CMD_FIRST_VALID; Commands[i].Name; i++)
    {
        if (STRSTR (Commands[i].Name, UserCommand) == Commands[i].Name)
        {
            return (i);
        }
    }

    /* Command not recognized */

    return (CMD_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbCommandDispatch
 *
 * PARAMETERS:  InputBuffer         - Command line buffer
 *              WalkState           - Current walk
 *              Op                  - Current (executing) parse op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Command dispatcher.  Called from two places:
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbCommandDispatch (
    NATIVE_CHAR             *InputBuffer,
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  Temp;
    UINT32                  CommandIndex;
    UINT32                  ParamCount;
    NATIVE_CHAR             *CommandLine;
    ACPI_STATUS             Status = AE_CTRL_TRUE;


    /* If AcpiTerminate has been called, terminate this thread */

    if (AcpiGbl_DbTerminateThreads)
    {
        return (AE_CTRL_TERMINATE);
    }

    ParamCount = AcpiDbGetLine (InputBuffer);
    CommandIndex = AcpiDbMatchCommand (Args[0]);
    Temp = 0;

    /* Verify that we have the minimum number of params */

    if (ParamCount < Commands[CommandIndex].MinArgs)
    {
        AcpiOsPrintf ("%d parameters entered, [%s] requires %d parameters\n",
                        ParamCount, Commands[CommandIndex].Name, Commands[CommandIndex].MinArgs);
        return (AE_CTRL_TRUE);
    }

    /* Decode and dispatch the command */

    switch (CommandIndex)
    {
    case CMD_NULL:
        if (Op)
        {
            return (AE_OK);
        }
        break;

    case CMD_ALLOCATIONS:
        AcpiCmDumpCurrentAllocations ((UINT32) -1, NULL);
        break;

    case CMD_ARGS:
    case CMD_ARGUMENTS:
        AcpiDbDisplayArguments ();
        break;

    case CMD_BREAKPOINT:
        AcpiDbSetMethodBreakpoint (Args[1], WalkState, Op);
        break;

    case CMD_CALL:
        AcpiDbSetMethodCallBreakpoint (Op);
        Status = AE_OK;
        break;

    case CMD_CLOSE:
        AcpiDbCloseDebugFile ();
        break;

    case CMD_DEBUG:
        AcpiDbExecute (Args[1], &Args[2], EX_SINGLE_STEP);
        break;

    case CMD_DUMP:
        AcpiDbDecodeAndDisplayObject (Args[1], Args[2]);
        break;

    case CMD_ENABLEACPI:
        Status = AcpiEnable();
        if (ACPI_FAILURE(Status))
        {
            AcpiOsPrintf("AcpiEnable failed (Status=%X)\n", Status);
            return (Status);
        }
        break;

    case CMD_EVENT:
        AcpiOsPrintf ("Event command not implemented\n");
        break;

    case CMD_EXECUTE:
        AcpiDbExecute (Args[1], &Args[2], EX_NO_SINGLE_STEP);
        break;

    case CMD_FIND:
        AcpiDbFindNameInNamespace (Args[1]);
        break;

    case CMD_GO:
        AcpiGbl_CmSingleStep = FALSE;
        return (AE_OK);

    case CMD_HELP:
    case CMD_HELP2:
        AcpiDbDisplayHelp (Args[1]);
        break;

    case CMD_HISTORY:
        AcpiDbDisplayHistory ();
        break;

    case CMD_HISTORY_EXE:
        CommandLine = AcpiDbGetFromHistory (Args[1]);
        if (!CommandLine)
        {
            return (AE_CTRL_TRUE);
        }

        Status = AcpiDbCommandDispatch (CommandLine, WalkState, Op);
        if (ACPI_SUCCESS (Status))
            Status = AE_CTRL_TRUE;
        return (Status);
        break;

    case CMD_HISTORY_LAST:
        CommandLine = AcpiDbGetFromHistory (NULL);
        if (!CommandLine)
        {
            return (AE_CTRL_TRUE);
        }

        Status = AcpiDbCommandDispatch (CommandLine, WalkState, Op);
        if (ACPI_SUCCESS (Status))
            Status = AE_CTRL_TRUE;
        return (Status);

    case CMD_INFORMATION:
        AcpiDbDisplayMethodInfo (Op);
        break;

    case CMD_INTO:
        if (Op)
        {
            AcpiGbl_CmSingleStep = TRUE;

/* TBD: Must get current walk state */
            /* AcpiGbl_MethodBreakpoint = 0; */
            return (AE_OK);
        }
        break;

    case CMD_LEVEL:
        if (ParamCount == 0)
        {
            AcpiOsPrintf ("Current debug level for file output is:    %8.8lX\n", AcpiGbl_DbDebugLevel);
            AcpiOsPrintf ("Current debug level for console output is: %8.8lX\n", AcpiGbl_DbConsoleDebugLevel);
        }
        else if (ParamCount == 2)
        {
            Temp = AcpiGbl_DbConsoleDebugLevel;
            AcpiGbl_DbConsoleDebugLevel = STRTOUL (Args[1], NULL, 16);
            AcpiOsPrintf ("Debug Level for console output was %8.8lX, now %8.8lX\n", Temp, AcpiGbl_DbConsoleDebugLevel);
        }
        else
        {
            Temp = AcpiGbl_DbDebugLevel;
            AcpiGbl_DbDebugLevel = STRTOUL (Args[1], NULL, 16);
            AcpiOsPrintf ("Debug Level for file output was %8.8lX, now %8.8lX\n", Temp, AcpiGbl_DbDebugLevel);
        }
        break;

    case CMD_LIST:
        AcpiDbDisassembleAml (Args[1], Op);
        break;

    case CMD_LOAD:
        Status = AcpiDbLoadAcpiTable (Args[1]);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        break;

    case CMD_LOCALS:
        AcpiDbDisplayLocals ();
        break;

    case CMD_METHODS:
        AcpiDbDisplayObjects ("METHOD", Args[1]);
        break;

    case CMD_NAMESPACE:
        AcpiDbDumpNamespace (Args[1], Args[2]);
        break;

    case CMD_NOTIFY:
        Temp = STRTOUL (Args[2], NULL, 0);
        AcpiDbSendNotify (Args[1], Temp);
        break;

    case CMD_OBJECT:
        AcpiDbDisplayObjects (Args[1], Args[2]);
        break;

    case CMD_OPEN:
        AcpiDbOpenDebugFile (Args[1]);
        break;

    case CMD_OWNER:
        AcpiDbDumpNamespaceByOwner (Args[1], Args[2]);
        break;

    case CMD_PREFIX:
        AcpiDbSetScope (Args[1]);
        break;

    case CMD_REFERENCES:
        AcpiDbFindReferences (Args[1]);
        break;

    case CMD_RESULTS:
        AcpiDbDisplayResults ();
        break;

    case CMD_SET:
        AcpiDbSetMethodData (Args[1], Args[2], Args[3]);
        break;

    case CMD_STATS:
        AcpiDbDisplayStatistics (Args[1]);
        break;

    case CMD_STOP:
        return (AE_AML_ERROR);
        break;

    case CMD_TABLES:
        AcpiDbDisplayTableInfo (Args[1]);
        break;

    case CMD_TERMINATE:
        AcpiDbSetOutputDestination (DB_REDIRECTABLE_OUTPUT);
        AcpiCmSubsystemShutdown ();

        /* TBD: [Restructure] Need some way to re-initialize without re-creating the semaphores! */

        /*  AcpiInitialize (NULL);  */
        break;

    case CMD_THREADS:
        AcpiDbCreateExecutionThreads (Args[1], Args[2], Args[3]);
        break;

    case CMD_TREE:
        AcpiDbDisplayCallingTree ();
        break;

    case CMD_UNLOAD:
        AcpiDbUnloadAcpiTable (Args[1], Args[2]);
        break;

    case CMD_EXIT:
    case CMD_QUIT:
        if (Op)
        {
            return (AE_AML_ERROR);
        }

        if (!OutputToFile)
        {
            AcpiDbgLevel = DEBUG_DEFAULT;
        }

        /* Shutdown */

        /* AcpiCmSubsystemShutdown (); */
        AcpiDbCloseDebugFile ();

        AcpiGbl_DbTerminateThreads = TRUE;

        return (AE_CTRL_TERMINATE);

    case CMD_NOT_FOUND:
        AcpiOsPrintf ("Unknown Command\n");
        return (AE_CTRL_TRUE);
    }


    /* Add all commands that come here to the history buffer */

    AcpiDbAddToHistory (InputBuffer);
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbExecuteThread
 *
 * PARAMETERS:  Context         - Not used
 *
 * RETURN:      None
 *
 * DESCRIPTION: Debugger execute thread.  Waits for a command line, then
 *              simply dispatches it.
 *
 ******************************************************************************/

void
AcpiDbExecuteThread (
    void                    *Context)
{
    ACPI_STATUS             Status = AE_OK;


    while (Status != AE_CTRL_TERMINATE)
    {
        AcpiGbl_MethodExecuting = FALSE;
        AcpiGbl_StepToNextCall = FALSE;

        AcpiCmAcquireMutex (ACPI_MTX_DEBUG_CMD_READY);
        Status = AcpiDbCommandDispatch (LineBuf, NULL, NULL);
        AcpiCmReleaseMutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSingleThread
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Debugger execute thread.  Waits for a command line, then
 *              simply dispatches it.
 *
 ******************************************************************************/

void
AcpiDbSingleThread (
    void)
{
    ACPI_STATUS             Status = AE_OK;


    AcpiGbl_MethodExecuting = FALSE;
    AcpiGbl_StepToNextCall = FALSE;

    Status = AcpiDbCommandDispatch (LineBuf, NULL, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbUserCommands
 *
 * PARAMETERS:  Prompt              - User prompt (depends on mode)
 *              Op                  - Current executing parse op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Command line execution for the AML debugger.  Commands are
 *              matched and dispatched here.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbUserCommands (
    NATIVE_CHAR             Prompt,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status = AE_OK;


    /* TBD: [Restructure] Need a separate command line buffer for step mode */

    while (!AcpiGbl_DbTerminateThreads)
    {
        /* Force output to console until a command is entered */

        AcpiDbSetOutputDestination (DB_CONSOLE_OUTPUT);

        /* Different prompt if method is executing */

        if (!AcpiGbl_MethodExecuting)
        {
            AcpiOsPrintf ("%1c ", DB_COMMAND_PROMPT);
        }
        else
        {
            AcpiOsPrintf ("%1c ", DB_EXECUTE_PROMPT);
        }

        /* Get the user input line */

        AcpiOsGetLine (LineBuf);


        /* Check for single or multithreaded debug */

        if (AcpiGbl_DebuggerConfiguration & DEBUGGER_MULTI_THREADED)
        {
            /*
             * Signal the debug thread that we have a command to execute,
             * and wait for the command to complete.
             */

            AcpiCmReleaseMutex (ACPI_MTX_DEBUG_CMD_READY);
            AcpiCmAcquireMutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
        }

        else
        {
            /* Just call to the command line interpreter */

            AcpiDbSingleThread ();
        }
    }


    /*
     * Only this thread (the original thread) should actually terminate the subsystem,
     * because all the semaphores are deleted during termination
     */
    AcpiTerminate ();

    return (Status);
}


#endif  /* ENABLE_DEBUGGER */

