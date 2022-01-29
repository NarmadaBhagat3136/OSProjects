
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

static const int kMaxDefinitionListSize = 16;
static const int kMaxUseListSize = 16;
static const int kMaxUseInstructionsSize = 512;
static const char* kDelimiters = " \t\n\r" ;
static const int kInvalidInstructionCodeOverflow = 9999;
static const int kInvalidInstructionCodeUnderflow = 0;
static const int kMemorySize = 512;
static const int kMaxOperand = 1000;
static const int kMaxOpCode = 10;

enum SyntaxError {
    ERROR_OK = -1,
    ERROR_NUM_EXPECTED, // Number expect 
    ERROR_SYM_EXPECTED, // Symbol Expected 
    ERROR_ADDR_EXPECTED, // Addressing Expected which is A/E/I/R  
    ERROR_SYM_TOO_LONG, // Symbol Name is too long 
    ERROR_TOO_MANY_DEF_IN_MODULE, // > 16  
    ERROR_TOO_MANY_USE_IN_MODULE, // > 16 
    ERROR_TOO_MANY_INSTR, // total num_instr exceeds memory size (512)    
};

namespace base {

// Data class for storing individual tokens in the compiled object file.
class Token {

public:
    Token(int line_num, int position, const std::string& token) 
        : line_num_(line_num), position_(position), 
        token_(token), err_(std::make_unique<SyntaxError>()) { *err_ = ERROR_OK; }

    // Location of line the token points to.
    int line_num() const { return line_num_; }

    // Position of token in the like.
    int position() const { return position_; }

    // String representing the token.
    const std::string& token() const { return token_; }

    bool ReadAsInt(int* int_token) const;

    bool ReadAsSymbol(std::string* str_token) const ;

    bool ReadAsIAER(char* char_token) const;

    void err(SyntaxError e) const { *err_ = e; }

    SyntaxError err() const {return *err_; }

private:
    int line_num_;
    int position_;
    std::string token_;
    std::unique_ptr<SyntaxError> err_;  // Last error when trying to parse this token.

    friend std::ostream& operator<<(std::ostream& os, const Token& dt);
};

bool TryParseInt(const std::string& s, int* result) {
    std::string::size_type sz;
    try {
        *result = std::stoi(s, &sz);
    } catch (std::exception& e) {
        return false;
    }
    // Handle case that stoi/strtol in C/C++ parse "42Hello".
    return sz == s.length();
}

string ErrorMessageForToken(const Token& token) {
    static const char* errstr[] = { 
        "NUM_EXPECTED", // Number expect 
        "SYM_EXPECTED", // Symbol Expected 
        "ADDR_EXPECTED", // Addressing Expected which is A/E/I/R  
        "SYM_TOO_LONG", // Symbol Name is too long 
        "TOO_MANY_DEF_IN_MODULE", // > 16  
        "TOO_MANY_USE_IN_MODULE", // > 16 
        "TOO_MANY_INSTR", // total num_instr exceeds memory size (512)   
    };
    if (token.err() == ERROR_OK) {
        return std::string("");
    }
    stringstream buffer;
    buffer << "Parse Error line " << token.line_num() << " offset "
        << token.position() << ": " << errstr[static_cast<int>(token.err())]
        << endl;
    return buffer.str();
}

ostream& operator<<(ostream& os, const Token& t) {
    os  << "Token: " << t.line_num() << ":" << t.position() << " : "
        << t.token();
    return os;
}

bool Token::ReadAsInt(int* int_token) const {
    if (!TryParseInt(token(), int_token)) {
        *err_ = ERROR_NUM_EXPECTED;
        return false;
    }
    return true;
}

bool Token::ReadAsSymbol(std::string* str_token) const {
    // Accepted symbols should be upto 16 characters long
    // (not including terminations e.g. ‘\0’), 
    if (token_.length() > 16) {
        *err_ = ERROR_SYM_TOO_LONG;
        return false;
    }
    // Symbol must follow [a-Z][a-Z0-9]*
    if (!std::regex_match(token_, std::regex("[a-zA-Z][a-zA-Z0-9]*"))) {
        *err_ = ERROR_SYM_EXPECTED;
        return false;
    }
    *str_token = token();
    return true;
}

bool Token::ReadAsIAER(char* char_token) const {
    if (token_.length() != 1) {
        *err_ = ERROR_ADDR_EXPECTED;
        return false;
    }
    char c = token_[0];
    if (c != 'I' && c != 'A' && c != 'E' && c != 'R') {
        *err_ = ERROR_ADDR_EXPECTED;
        return false;        
    }
    *char_token = c;
    return true;
}

}  // namespace base

namespace tokenizer {

// Data structure for Symbol value and metadata.
class SymbolData {
public:
    explicit SymbolData(int module, int sorting_index) 
        : module_(module), value_(0), used_(false),
          sorting_index_(sorting_index) {}

    int module() const { return module_; }

    std::string err() const { return err_; }
    void err(const std::string& e) { err_ = e; }

    int value() const { return value_; }
    void value(int v) { value_ = v; }
    bool used() { return used_; }
    void used(bool u) { used_ = u; }
    int sorting_index() { return sorting_index_; }
private:
    std::string err_;  // Any error/warning related to symbol.
    int value_;  // Symbol value.
    const int module_;  // Module where symbol is defined.
    bool used_;  // True if the symbol is used.
    const int sorting_index_;  // Index of symbol definition.
};


// Symbol table data struction to hold symbols between pass1 & pass2.
class SymbolTable {
public:
    // Add symbol to symbol table. Only called from pass 1.
    void AddSymbol(const std::string& symbol, int value, int module);
    // Check bounds on symbol value. Handles Rule 5.
    void VerifySymbol(
        int last_module, int last_module_size, int curr_module_index) const;
    // Prints symbol table to console.
    void Print() const;
    // Returns the value of symbol. Also mark it used if mark_use set.
    int Value(const std::string& symbol, bool mark_use) const;
    // Check is a symbol from symbol table is used. (end of pass 2).
    void VerifySymbolUsed() const;
private:
    // Holds symbols.
    std::map<std::string, std::unique_ptr<SymbolData>> symbol_value_;
};

bool PairComparer(const pair<string, int>& a,  const pair<string, int>& b) { 
    return a.second < b.second; 
}

void SymbolTable::AddSymbol(const string& symbol, int value, int module) {
    if (symbol_value_.find(symbol) != symbol_value_.end()) {
        symbol_value_[symbol]->err(
            "Error: This variable is multiple times defined; first value used");
        return;
    }
    symbol_value_.insert(std::make_pair(
        symbol, make_unique<SymbolData>(module, symbol_value_.size())));
    symbol_value_[symbol]->value(value);
}

void SymbolTable::VerifySymbol(
    int last_module, int last_module_size, int curr_module_index) const {
    int last_module_index = curr_module_index - last_module_size;
    for (const auto& kv : symbol_value_) {
        if (kv.second->module() != last_module)
            continue;
        int relative_value = kv.second->value() - last_module_index;
        if (relative_value >= last_module_size) {
            cout << "Warning: Module " << last_module <<": "
                << kv.first << " too big " << relative_value << " (max="
                << last_module_size - 1 << ") assume zero relative" << endl;
            kv.second->value(last_module_index);
        }
    }
}

void SymbolTable::VerifySymbolUsed() const {
    for (const auto& kv : symbol_value_) {
        if (!kv.second->used()) {
            cout << "Warning: Module " << kv.second->module() << ": "
                << kv.first << " was defined but never used"
                << endl;
        }
    }
}

void SymbolTable::Print() const {
    vector<pair<string, int>> ordered_symbols;
    for (const auto& kv : symbol_value_) {
        ordered_symbols.push_back(
            make_pair(kv.first, kv.second->sorting_index()));
    }
    sort(ordered_symbols.begin(), ordered_symbols.end(), PairComparer);
    cout << "Symbol Table" << endl;

    for (const auto& symbol : ordered_symbols) {
        const auto& symbol_data = symbol_value_.at(symbol.first);
        cout << symbol.first << "=" << symbol_data->value();
        if (!symbol_data->err().empty()) {
            cout << " " << symbol_data->err();
        }
        cout << endl;
    }
    cout << endl;
}

int SymbolTable::Value(const std::string& symbol, bool mark_use) const {
    if (symbol_value_.find(symbol) == symbol_value_.end()) {
        return -1;
    }
    if (mark_use) {
        symbol_value_.at(symbol)->used(true);
    }
    return symbol_value_.at(symbol)->value();
}

// Holds data related to use of a symbol is use list. This is used to detect
// is a symbol is not used.
class UseData {
public:
    explicit UseData(const std::string& symbol) 
        : symbol_(symbol), used_(false) {}
    bool used() const { return used_; }
    void used(bool u) {used_ = u; }
    std::string symbol() const { return symbol_; }
private:
    std::string symbol_;
    bool used_;
};

// Data structure to hold use list in a module. Resets at module change,
class UseList {
public:
    void AddSymbol(const std::string& symbol, int index);
    void Reset();
    bool Has(int index) const;
    std::vector<std::string> UnusedSymbols() const;
    const std::unique_ptr<UseData>& Get(int index);
private:
    std::map<int, std::unique_ptr<UseData>> use_list_;
};


void UseList::AddSymbol(const string& symbol, int index) {
    use_list_.insert(make_pair(index, make_unique<UseData>(symbol)));
}

void UseList::Reset() {
    use_list_.clear();
}

bool UseList::Has(int index) const {
    return (use_list_.find(index) != use_list_.end());
}

vector<string> UseList::UnusedSymbols() const {
    vector<string> unused_symbols;
    for (const auto& kv: use_list_) {
        if (!kv.second->used()) {
            unused_symbols.push_back(kv.second->symbol());
        }
    }
    return unused_symbols;
}

const unique_ptr<UseData>& UseList::Get(int index) {
    return use_list_.at(index);
}

// State machine states for parsing program file.
enum ParsingState {
    STATE_MODULE_START = 0,  // New module's def list starts. Read # of defs 
    STATE_READ_DEFINITION_SYMBOL,  // Reading definition symbol.
    STATE_READ_DEFINITION_VALUE,  // Reading deinition value.
    STATE_USE_LIST_START,  // Enters use list. Read # of use symbols.
    STATE_USE_LIST_READ,  // Reading use symbols.
    STATE_INSTRUCTION_LIST_START,  // Enters Program text. Read # of instr.
    STATE_INSTRUCTION_TYPE_READ,  // Read instruction type.
    STATE_INSTRUCTION_CODE_READ,  // Read <op_code, operand> value.
    STATE_SYNTAX_ERROR,  // End state in case parsing encounters error.
    STATE_TERMINATED  // End state after parsing is successful.
};

// Handles processing file. Conceptualizes a parsing state machine that
// implements the logic for parsing and interpreting tokens. Also
// handles syntax errors when parsing token.
class ParsingContext {
public:
    ParsingContext()
        : module_index_(0), module_count_(0), definition_count_(0),
          definition_read_(0), use_list_read_(0), use_list_count_(0), 
          instruction_count_(0), instruction_read_(0),
          last_module_instruction_count_(0),
          current_state_(STATE_MODULE_START), next_state_(STATE_MODULE_START),
          index_(1), position_(1) {}

    // Public getters.
    int module_index() const { return module_index_; }
    int module_count() const { return module_count_; }
    std::string last_symbol() const { return last_symbol_; }
    char last_instruction() const { return last_instruction_; }
    int last_module_instruction_count() const {
        return last_module_instruction_count_;
    }
    int instruction_count() const {return instruction_count_; }
    int instruction_index() const {return instruction_read_ - 1; }
    int use_list_index() const {return use_list_read_ - 1; }

    ParsingState current_state() const { return current_state_; }
    ParsingState next_state() const { return next_state_; }

    void ProcessState(const base::Token& token);
    void AdvanceState() { current_state_ = next_state_; }
    void HandleEnd();
    int index() const { return index_; }
    void index(int i) { index_ = i; }

    int position() const { return position_; }
    void position(int position) { position_ = position; }
private:
    // Handle end of previous module and start new module.
    void HandleModuleStart(const base::Token& token);

    void HandleReadDefinitionSymbol(const base::Token& token);
    void HandleReadDefinitionValue(const base::Token& token);

    void HandleUseListStart(const base::Token& token);

    void HandleUseListRead(const base::Token& token);

    void HandleInstructionListStart(const base::Token& token);

    void HandleInstructionTypeRead(const base::Token& token);
    void HandleInstructionCodeRead(const base::Token& token);

    int module_index_;  // Memory memory index. Number of instructions before.
    int module_count_;  // Number of modules parsed so far.
    std::string last_symbol_;  // Last symbol when reading Definition list.
    char last_instruction_; // Last instruction when reading instruction list.
    int definition_read_;  // Number of definitions processed for the module.
    int definition_count_;  // Expected size of definition list.
    int use_list_read_;  // Number of use list symbols processed. 
    int use_list_count_;  // Expected size of use list. 
    int instruction_count_;  // Expected size of instruction list in the module.
    int instruction_read_;  // Number of instruction read.
    int last_module_instruction_count_;  // Instructions in last module.

    ParsingState current_state_;  // State at the beginning of parsing token.
    ParsingState next_state_;  // State after ProcessToken.

    // Current location in file.
    int index_;
    int position_;
};

void ParsingContext::HandleEnd() {
    if (current_state_ != STATE_MODULE_START) {
        next_state_ = current_state_;
        return;
    }
    module_count_++;  // Increase module count.
    module_index_ += instruction_count_;  // Store start index of the module.
    last_symbol_ = "";
    last_instruction_ = '\0';
    definition_read_ = 0;
    use_list_read_ = 0;
    use_list_count_ = 0;
    last_module_instruction_count_ = instruction_count_;
    instruction_count_ = 0;
    instruction_read_ = 0;
    last_symbol_ = "";
    next_state_ = STATE_TERMINATED;
}

void ParsingContext::HandleModuleStart(const base::Token& token) {
    // First section at the start of module must be definition
    // and the token must point to its size which should be between 0 and 16.
    if (!token.ReadAsInt(&definition_count_)) {
        next_state_ = STATE_SYNTAX_ERROR;
        return;
    }
    if (definition_count_ > kMaxDefinitionListSize) {
        next_state_ = STATE_SYNTAX_ERROR;
        token.err(ERROR_TOO_MANY_DEF_IN_MODULE);
        return;
    }
    module_count_++;  // Increase module count.
    module_index_ += instruction_count_;  // Store start index of the module.
    last_symbol_ = "";
    last_instruction_ = '\0';
    definition_read_ = 0;
    use_list_read_ = 0;
    use_list_count_ = 0;
    last_module_instruction_count_ = instruction_count_;
    instruction_count_ = 0;
    instruction_read_ = 0;
    last_symbol_ = "";
    if (definition_count_ != 0) {
        next_state_ = STATE_READ_DEFINITION_SYMBOL;
    } else {
        next_state_ = STATE_USE_LIST_START;
    }
}

void ParsingContext::HandleReadDefinitionSymbol(const base::Token& token) {
    if (!token.ReadAsSymbol(&last_symbol_)) {
        next_state_ = STATE_SYNTAX_ERROR;
    } else {
        next_state_ = STATE_READ_DEFINITION_VALUE;
    }
}

void ParsingContext::HandleReadDefinitionValue(const base::Token& token) {
    int value;
    if (!token.ReadAsInt(&value)) {
        next_state_ = STATE_SYNTAX_ERROR;
        return;
    }
    if (++definition_read_ == definition_count_) {
        next_state_ = STATE_USE_LIST_START;
    } else {
        next_state_ = STATE_READ_DEFINITION_SYMBOL;
    }
}

void ParsingContext::HandleUseListStart(const base::Token& token) {
    if (!token.ReadAsInt(&use_list_count_)) {
        next_state_ = STATE_SYNTAX_ERROR;
        return;
    }
    if (use_list_count_ > kMaxUseListSize) {
        next_state_ = STATE_SYNTAX_ERROR;
        token.err(ERROR_TOO_MANY_USE_IN_MODULE);
        return;
    }
    if (use_list_count_ == 0) {
        next_state_ = STATE_INSTRUCTION_LIST_START;
    } else {
        next_state_ = STATE_USE_LIST_READ;
    }
}

void ParsingContext::HandleUseListRead(const base::Token& token) {
    std::string symbol;
    if (!token.ReadAsSymbol(&symbol)) {
        next_state_ = STATE_SYNTAX_ERROR;
        return;
    }
    if (++use_list_read_ == use_list_count_) {
        next_state_ = STATE_INSTRUCTION_LIST_START;
    } else {
        next_state_ = STATE_USE_LIST_READ;
    }
}

void ParsingContext::HandleInstructionListStart(const base::Token& token) {
    if (!token.ReadAsInt(&instruction_count_)) {
        next_state_ = STATE_SYNTAX_ERROR;
        return;
    }
    if (instruction_count_ +  module_index_ > kMaxUseInstructionsSize) {
        next_state_ = STATE_SYNTAX_ERROR;
        token.err(ERROR_TOO_MANY_INSTR);
        return;
    }
    if (instruction_count_ != 0)
        next_state_ = STATE_INSTRUCTION_TYPE_READ;
    else
        next_state_ = STATE_MODULE_START;
}

void ParsingContext::HandleInstructionTypeRead(const base::Token& token) {
    if (!token.ReadAsIAER(&last_instruction_)) {
        next_state_ = STATE_SYNTAX_ERROR;
    } else {
        next_state_ = STATE_INSTRUCTION_CODE_READ;
    }
}

void ParsingContext::HandleInstructionCodeRead(const base::Token& token) {
    int code;
    if (!token.ReadAsInt(&code)) {
        next_state_ = STATE_SYNTAX_ERROR;
        return;
    }
    if (++instruction_read_ == instruction_count_) {
        next_state_ = STATE_MODULE_START;
    } else {
        next_state_ = STATE_INSTRUCTION_TYPE_READ;
    }
}

void ParsingContext::ProcessState(const base::Token& token) {
    // cout << token << " : " << current_state_ << endl;
    switch(current_state_) {
        case STATE_MODULE_START:
            HandleModuleStart(token); break;
        case STATE_READ_DEFINITION_SYMBOL:
            HandleReadDefinitionSymbol(token); break;
        case STATE_READ_DEFINITION_VALUE:
            HandleReadDefinitionValue(token); break;
        case STATE_USE_LIST_START:
            HandleUseListStart(token); break;
        case STATE_USE_LIST_READ:
            HandleUseListRead(token); break;
        case STATE_INSTRUCTION_LIST_START:
            HandleInstructionListStart(token); break;
        case STATE_INSTRUCTION_TYPE_READ:
            HandleInstructionTypeRead(token); break;
        case STATE_INSTRUCTION_CODE_READ:
            HandleInstructionCodeRead(token); break;
        case STATE_SYNTAX_ERROR:
            break;
    }
}

class TokenProcessor {

public:
    // Hooks that can run after tokenizer creates a token.
    // During it's implementation we can assume that token is syntactically
    // correct. If token fails syntax check then ProcessToken will not be
    // called and instead a runtime_error will thrown with information.
    // Args:
    //   token: This is the token that has just been parsed by tokenizer.
    //   context: Various information about the state of parsing. "context"
    //       object should be ReadOnly here and is owned by tokenizer.
    //       This contain information like line number, offset, module
    //       number, parsing state and also save information of previous
    //       token or previous module.
    //   symbol_table: A SymbolTable object, This will be modified only
    //       during pass1 and during pass2 it will be ReadOnly. SymbolTable
    //       object is ReadOnly for the Tokenizer and is not modifed during
    //       parsing. TokenProcessor for pass1 will insert symbols in it, while
    //       TokenProcessor for pass2 will just read the symbols.
    virtual void ProcessToken(
        const base::Token& token,
        const std::unique_ptr<ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) = 0;

    // Hooks to provide custom book keeping logic when parsing is completed.
    // Any warning message that is to be handled at the end of the pass
    // will be implemented here.
    virtual void Stop(
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list) = 0;
};

class Tokenizer {
public:

    Tokenizer(
        const std::string& filename,
        std::unique_ptr<TokenProcessor> processor,
        std::unique_ptr<SymbolTable> symbol_table);

    virtual ~Tokenizer();

    void TokenizeFile();

    const std::unique_ptr<ParsingContext>& context() const {
        return context_; 
    }
    std::unique_ptr<SymbolTable>& symbol_table() {
        return symbol_table_;
    }

private:

    void TokenizeLine(const std::string& line);

    std::unique_ptr<TokenProcessor> token_processor_;
    const std::string filename_;
    std::ifstream stream_;
    std::unique_ptr<ParsingContext> context_;
    std::unique_ptr<SymbolTable> symbol_table_;
    std::unique_ptr<UseList> use_list_;
};

Tokenizer::Tokenizer(
    const std::string& filename, 
    std::unique_ptr<TokenProcessor> processor,
    std::unique_ptr<SymbolTable> symbol_table)
    : filename_(filename), stream_(filename),
      context_(make_unique<ParsingContext>()),
      symbol_table_(std::move(symbol_table)),
      token_processor_(std::move(processor)),
      use_list_(make_unique<UseList>()) { }

Tokenizer::~Tokenizer() {
    stream_.close();
}

void Tokenizer::TokenizeLine(const string& line) {
    unique_ptr<char[]> cline(new char[line.length() + 1]);
    strcpy(cline.get(), line.c_str());
    char* next_token = strtok(cline.get(), kDelimiters);
    context_->position(1);
    while (next_token != NULL) {
        int token_start = next_token - cline.get() + 1;
        base::Token t(context_->index(), token_start, string(next_token));
        context_->ProcessState(t);
        if (context_->next_state() == STATE_SYNTAX_ERROR) {
            // Abort parsing on recieving syntax error.
            context_->position(token_start + strlen(next_token));
            context_->AdvanceState();
            throw runtime_error(base::ErrorMessageForToken(t));
        }
        token_processor_->ProcessToken(t, context_, symbol_table_, use_list_);
        context_->position(token_start + strlen(next_token));
        context_->AdvanceState();

        next_token = strtok(NULL, kDelimiters);
    }
    context_->position(1 + line.length());
}

void Tokenizer::TokenizeFile() {
    string line;
    while(getline(stream_, line)) {
        TokenizeLine(line);
        context_->index(context_->index() + 1);  // Increase line index.
    }
    // Move index to last line in case of EOF.
    if (!stream_.bad() && stream_.eof())
        context_->index(context_->index() - 1);
    context_->HandleEnd();
    if (context_->next_state() != STATE_TERMINATED) {
        // Create an empty token and let the Parsing context
        // handle this until a Syntax error is encountered or
        // the parsing state machine terminates.
        base::Token t(context_->index(), context_->position(), "");
        context_->ProcessState(t);
        // Abort parsing as file is missing data to process.
        throw runtime_error(base::ErrorMessageForToken(t));
    }
    token_processor_->Stop(context_, symbol_table_, use_list_);
}


}  // namespace tokenizer

namespace linker {

class PrintToken : public tokenizer::TokenProcessor {
public:
    void ProcessToken(
        const base::Token& token,
        const unique_ptr<tokenizer::ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) {
        cout << token << endl;
    }
    void Stop(
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list) {
        cout << "Final Spot in File : line=" 
            << context->index() << " offset=" << context->position() << endl;
    }
};

class SymbolTableGenerator : public tokenizer::TokenProcessor {
public:
    void ProcessToken(
            const base::Token& token,
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list);
    void Stop(
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list);
private:
    void HandleModuleChange(
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list);
};

void SymbolTableGenerator::ProcessToken(
        const base::Token& token,
        const std::unique_ptr<tokenizer::ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) {
    if (context->current_state() ==
        tokenizer::STATE_READ_DEFINITION_VALUE) {
        int value;
        token.ReadAsInt(&value);  // Processor won't see syntax error.
        int absolute_value = value + context->module_index();
        symbol_table->AddSymbol(
            context->last_symbol(), absolute_value,
            context->module_count());
    }
    if (context->current_state() == tokenizer::STATE_MODULE_START) {
        HandleModuleChange(context, symbol_table, use_list);
    }
}

void SymbolTableGenerator::Stop(
        const std::unique_ptr<tokenizer::ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) {
    HandleModuleChange(context, symbol_table, use_list);
}

void SymbolTableGenerator::HandleModuleChange(
        const std::unique_ptr<tokenizer::ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) {
    int module_size = context->last_module_instruction_count();
    int last_module_number = context->module_count() - 1;
    if (last_module_number < 0)
        return;
    // Rule 5: Verify that all the symbols added in this module
    // where within the module size.
    symbol_table->VerifySymbol(
        last_module_number, module_size, context->module_index());
}


class InstructionGenerator : public tokenizer::TokenProcessor {
public:
    virtual void Stop(
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list) override;
    virtual void ProcessToken(
            const base::Token& token,
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list) override;
private:

    void HandleModuleChange(
            const std::unique_ptr<tokenizer::ParsingContext>& context,
            const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
            const std::unique_ptr<tokenizer::UseList>& use_list);
};

// Prints warning at the end of pass 2.
void InstructionGenerator::Stop(
        const std::unique_ptr<tokenizer::ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) {
    HandleModuleChange(context, symbol_table, use_list);
    // Rule 4: Verify all symbols are used.
    // If a symbol is defined but not used, print a warning message & continue.
    symbol_table->VerifySymbolUsed();
}

// Main logic for pass 2.
void InstructionGenerator::ProcessToken(
        const base::Token& token,
        const std::unique_ptr<tokenizer::ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) {
    if (context->current_state() == tokenizer::STATE_MODULE_START) {
        // New module is starting, update use_list and handle rule 7 (unused
        // symbols from last module).
        HandleModuleChange(context, symbol_table, use_list);
    }
    if (context->current_state() == tokenizer::STATE_USE_LIST_READ) {
        // Parsing the use list. Add these symbols into use_list.
        string symbol;
        token.ReadAsSymbol(&symbol);
        use_list->AddSymbol(symbol, context->use_list_index());
    }
    if (context->current_state() ==
        tokenizer::STATE_INSTRUCTION_CODE_READ) {
        // Succesfully read a <Instruction Type: Instruction Code> pair.
        // Output it in mmeory map.
        char instruction_type = context->last_instruction();
        int instruction;
        token.ReadAsInt(&instruction);  // A Processor won't see syntax error.
        int op_code = instruction / kMaxOperand;
        int operand = instruction % kMaxOperand;
        string err;
        // Instruction code I doesn't have an op_code. For every other
        // instruction type, the op_code must be less than 10. (Rule 11).
        if (op_code >= kMaxOpCode && instruction_type != 'I') {
            // Rule: 11 Change instruction to the largest instruction.
            instruction = kInvalidInstructionCodeOverflow;
            err = "Error: Illegal opcode; treated as 9999";
        } else {
            switch(instruction_type) {
            case 'A':  
                // Instruction type A is left unchanged unless the operand
                // exceed memory size.
                if (operand >= kMemorySize) {
                    // Rule: 8
                    instruction =
                        kMaxOperand * op_code + kInvalidInstructionCodeUnderflow;
                    err = "Error: Absolute address exceeds machine size; zero used";
                }
                break;
            case 'I':
                // Intruction type I is left unchanged except when memory
                // overflow.
                if (instruction > kInvalidInstructionCodeOverflow) {
                    // Rule: 10
                    instruction = kInvalidInstructionCodeOverflow;
                    err = "Error: Illegal immediate value; treated as 9999";
                }
                break;
            case 'R':
                // Relative instructions added to module index. Note that
                // relative address can't exceed number of instruction in
                // the module. Also total number of isntruction is 512. Thus
                // the operand will never exceed 512.
                if (operand >= context->instruction_count()) {
                    // Rule: 9
                    operand = kInvalidInstructionCodeUnderflow;
                    err = "Error: Relative address exceeds module size; zero used";
                }
                operand += context->module_index();
                instruction = kMaxOperand * op_code + operand;
                break;
            case 'E':
                if (!use_list->Has(operand)) {
                    // Rule 6: If an external address is too large to
                    // reference an entry in the use list, print an error
                    // message and treat the address as immediate.
                    err = ("Error: External address exceeds length of "
                        "uselist; treated as immediate");
                    break;
                }
                // Map appress using external symbols.
                auto& extern_symbol = use_list->Get(operand);
                operand = symbol_table->Value(
                    extern_symbol->symbol(), true);
                if (operand == -1) {
                    // Rule 3: Symbol value doesn't exist.
                    operand = kInvalidInstructionCodeUnderflow;
                    err = ("Error: " + extern_symbol->symbol() +
                        " is not defined; zero used");
                }
                extern_symbol->used(true);
                instruction = kMaxOperand * op_code + operand;
                break;
            }
        }
        // Print instruction on console.
        int instruction_index =
            context->module_index() + context->instruction_index();
        cout << std::setfill('0') << std::setw(3) << instruction_index;
        cout << ": " << std::setfill('0') << std::setw(4) << instruction;
        if (!err.empty()) {
            cout << " " << err;
        }
        cout << endl;
    }
}

// This is called at module boundary and when the last module is processed.
// Prints warning if a symbol in use list wasn't referenced from instruction
// list. (Rule 7).
// It will also resets the use list as call to this marks the beginning
// of ny module.
void InstructionGenerator::HandleModuleChange(
        const std::unique_ptr<tokenizer::ParsingContext>& context,
        const std::unique_ptr<tokenizer::SymbolTable>& symbol_table,
        const std::unique_ptr<tokenizer::UseList>& use_list) {
        // Rule 7 Symbols used.
    auto unused_symbols = use_list->UnusedSymbols();
    for (const auto& unused_symbol: unused_symbols) {
        cout << "Warning: Module " << (context->module_count() - 1)
             << ": " << unused_symbol
             << " appeared in the uselist but was not actually used"
             << endl;
    }
    use_list->Reset();
}

}  // namespace linker



int main(int argc, char* argv[]) {
    // Stores filename. (Will throw exception if commandline not provided.)
    string filename(argv[1]);

    // ==================== PASS 1 ==================================

    // Tokenizer class abstracts the parsing logic and provide a
    // pluggable TokenProcessor interface that implements bussiness logic.
    // 'ProcessToken' is invoked for every parsed Token and 'Stop' is called
    // at the end of the parsing.
    //
    // Tokenizer class owns throwing SyntaxError if it encounters and invalid
    // token or EOF when it is expecting more tokens.
    //
    // Tokenizer class accepts an object of SymbolTable that will be provided
    // to TokenProcessor::ProcessToken. Tokenizer during pass1 is created
    // with a new SymbolTable, whose ownership is transferred to the 
    // pass2 tokenizer.
    tokenizer::Tokenizer pass1(
        filename, make_unique<linker::SymbolTableGenerator>(),
        make_unique<tokenizer::SymbolTable>());
    try {
        // Internally calls the SymbolTableGenerator logic while processing
        // tokens for the first pass. The ProcessToken in SymbolTableGenerator
        // mainly process tokens from the Def list in each module and
        // Stores symbol = value in symbol table. It also handles error
        // rule 2 and warning rule 5. Also any syntax error will be thrown
        // during this pass. Syntax errors are handled from ParsingContext
        // object owned directly by the tokenizer.
        pass1.TokenizeFile();
    } catch (const runtime_error& e) {
        // Catch syntax errors and terminate.
        cout << e.what() << endl;
        return 0;
    }

    // Prints SymbolTable portion of the linker output. (Including warnings)
    pass1.symbol_table()->Print();

    // ====================== PASS 2 =================================

    // Start the Memory Map section of the linker output.
    cout << "Memory Map" << endl;
    // A new Tokenizer object is created which takes the ownership of
    // SymbolTable generated from pass1. We are creating a new object instead of
    // reseting the tokenizer for pass1 to
    // ensure no data other than SymbolTable is transferred between pass1
    // & pass2.
    //
    // The TokenProcessor for this pass is InstructionGenerator which
    // handles parsing the RIAE instructions and generating the memory map.
    tokenizer::Tokenizer pass2(
        filename, make_unique<linker::InstructionGenerator>(),
        std::move(pass1.symbol_table()));
    try {
        // Internally calls the InstructionGenerator logic while processing
        // tokens for the second pass. The ProcessToken in InstructionGenerator
        // process tokens from the Use list and program text and mostly ignore
        // Def list. These will translate the instruction's memory addresses
        // and print them on console.
        // This handles error rule 3, 6, 8, 9, 10 & 11 and warnings 7 & 4.
        // The warning 4 is handled in the Stop function.
        // Note that the try catch here is not needed as SyntaxError
        // won't be thrown here as any Syntax error should be caught in pass1.
        // The parsing logic in pass2 is identical to pass1 as the Tokenizer
        // can't distinguish if it is running pass1 or pass2.
        pass2.TokenizeFile();
    } catch (const runtime_error& e) {
        cout << e.what() << endl;  // No error expected here.
        return 0;
    }

    return 0;
}