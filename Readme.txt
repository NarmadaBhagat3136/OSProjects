Step1:Please unzip the file.
Step2:make clean
Step3:make linker
Step4:Go to the path of the file with input files, runit and gradit files.
step5:Execute the test script(runit and gradit script).

NOTE: (Use following only in case required.)
Sometime when I try to run on linserv, I got following problem 
./linker: /lib64/libstdc++.so.6: version CXXABI_1.3.9' not found (required by ./linker)
./linker: /lib64/libstdc++.so.6: version GLIBCXX_3.4.26' not found (required by ./linker)
./linker: /lib64/libstdc++.so.6: version `GLIBCXX_3.4.21' not found (required by ./linker)

I have compiled using -static in makefile to prevent missing right c++ library version being loaded as these library should be statically linked, 
So I am expecting this error not to appear. But in case any issue happens, compile the file manually using following command-

g++-9.1 -std=c++17 -g -static linker.cc -o linker

Please note above case is needed only in case the binary built from "make linker" doesn't load the required library files for compile and all the test case fails.
I have already fixed this issue in my makefile and tested it.


A brief description of the approach I took to solve the lab assignment(Programming) -

I have created a Tokenizer class that takes care of parsing and syntax checking input program.

The Tokenizer class accepts a TokenProcessor object which provides "ProcessToken" function.
This function is invoked every time Tokenizer class successfully parse a Token.
When invoked from pass1 it creates SymbolTable by processing tokens from the def list.
New Tokenizer object is created for pass2. The ownership of SymbolTable is passed from pass1 to pass2.
No data other than symbol table is passed from pass1 to pass2. (This is ensured by creating a new ParsingContext object when
new Tokenizer object is created).
ParsingContext object is the container for all the data assiciated with the parsing state of the input file. It implements
state transition logic using following states.

    STATE_MODULE_START: New module's def list starts. Read # of defs
    STATE_READ_DEFINITION_SYMBOL: Reading definition symbol.
    STATE_READ_DEFINITION_VALUE: Reading deinition value.
    STATE_USE_LIST_START: Enters use list. Read # of use symbols.
    STATE_USE_LIST_READ: Reading use symbols.
    STATE_INSTRUCTION_LIST_START: Enters Program text. Read # of instr.
    STATE_INSTRUCTION_TYPE_READ: Read instruction type.
    STATE_INSTRUCTION_CODE_READ: Read <op_code, operand> value.
    STATE_SYNTAX_ERROR: End state in case parsing encounters error.
    STATE_TERMINATED: End state after parsing is successful.

The state transition implemented is as follows.  
START ->
Repeat (READ_DEFINITION_SYMBOL -> READ_DEFINITION_VALUE) for # definitions read at START ->
USE_LIST_START ->
Repeat (USE_LIST_READ) for # of use list read at USE_LIST_START ->
INSTRUCTION_LIST_START ->
Repeat (INSTRUCTION_TYPE_READ -> INSTRUCTION_CODE_READ) for # of instructions read at INSTRUCTION_LIST_START ->
START If there are more modules otherwise TERMINATED

If any error is encountered when running the above state machine, we enter SYNTAX_ERROR state. Where the state machine terminates and throws error.
