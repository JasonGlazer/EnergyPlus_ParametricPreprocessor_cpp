// ParametricPreprocessor
//
// C++ port of the EnergyPlus ParametricPreprocessor originally written in Fortran by
// Jason Glazer of GARD Analytics, Inc. (April 2009).
//
// PURPOSE OF THIS PROGRAM:
//   Implement the preprocessor for simplified parameter substitution that
//   allows some limited parameter substitution and equation evaluation
//   for multiple simulations using a single input file. Any field in the
//   input file can start with an equals sign "=" as a flag for a field with
//   an expression and can contain a reference to a parameter such as $WIDTH.
//   Specific objects that are used to set parameters include:
//
//        Parametric:SetValueForRun
//        Parametric:Logic
//        Parametric:RunControl
//        Parametric:FileNameSuffix
//
//   The order of evaluation is:
//      first set values based on Parametric:SetValueForRun
//      second evaluate all lines of the Parametric:Logic object
//      third evaluate all embedded expressions in remainder of file
//
// METHODOLOGY EMPLOYED:
//   Scan the input file and create an intermediate file that contains references
//   to all embedded expressions and has none of the Parametric:* objects. The program
//   stores the embedded expressions and lines from the Parametric:Logic object into
//   an intermediate code based on reverse polish notation. Then for each case, the
//   intermediate file is opened, the computations are done by first taking the
//   SetValueForRun values for the case, then the Parametric:Logic lines are computed,
//   and the embedded expressions are evaluated. The resulting evaluated symbols are
//   substituted in for the case. This is repeated for each case with a new file each
//   time.
//
// PORTING NOTES:
//   The port intentionally reproduces the observable behavior of the Fortran program
//   (output files, error file, console messages) so that existing parametric input files
//   produce identical results. Quirks of the original that affect results are preserved
//   and marked with "FORTRAN-COMPAT" comments; see PORTING_NOTES.md for the list.
//   Most arrays are 1-based (index 0 is an unused placeholder) because the intermediate
//   code mixes positive symbol references with negative token constants and because
//   index arithmetic on fields and line numbers is carried over from the original.

#ifndef ParametricPreprocessor_hh_INCLUDED
#define ParametricPreprocessor_hh_INCLUDED

#include <fstream>
#include <string>
#include <vector>

namespace ParametricPreprocessor {

// Simple 1-based array: element 0 exists but is never used. Access is bounds checked.
template <typename T> class Array1
{
public:
    Array1() : m_data(1) {}
    int size() const { return static_cast<int>(m_data.size()) - 1; }
    bool empty() const { return size() == 0; }
    T &operator()(int i) { return m_data.at(static_cast<std::size_t>(i)); }
    T const &operator()(int i) const { return m_data.at(static_cast<std::size_t>(i)); }
    void push_back(T const &item) { m_data.push_back(item); }
    void resize(int n) { m_data.resize(static_cast<std::size_t>(n) + 1); }
    void clear() { m_data.resize(1); }

private:
    std::vector<T> m_data;
};

class Preprocessor
{
public:
    explicit Preprocessor(bool verboseDebug);

    // Process the input file. Returns the number of error and warning messages recorded.
    int run(std::string const &inputFilePathName);

    int numErrMsgs() const { return static_cast<int>(m_errMsgs.size()); }

private:
    // ---------------------------------------------------------------------------------
    // Data types
    // ---------------------------------------------------------------------------------
    struct OpStackType
    {
        int tokOperator = 0;
        int precedence = 0;
        int funcStart = 0;
        int funcEnd = 0;
    };

    struct ErrMsgType
    {
        std::string msgText;
        int msgKind = 0;
        int msgErrNum = 0;
        std::string msgContext;
    };

    // expressions found in fields of the IDF file
    struct FoundExpressionType
    {
        std::string text;
        int line = 0;
        int startIntCode = 0;
        int endIntCode = 0;
        std::string expResult;
    };

    // objects that are not stored but are simply identified by the line numbers
    struct ObjectLinesType
    {
        std::string kindOfObj;
        std::string nameOfObj; // first field
        int firstLine = 0;
        int lastLine = 0;
        bool enabled = true; // in parametric:logic either ENABLE or DISABLE for this object
    };

    // Parametric:* objects that are read and stored in memory
    struct IdfObjectType
    {
        int kindObj = 0;
        int firstField = 0;
        int lastField = 0;
    };

    struct CasesType
    {
        bool active = false;
        bool dup = false;
        bool dup2 = false;
        std::string suffix;
    };

    // The symbol table is referenced by the IntCode array and contains the active value
    // of each parameter and all constants.
    struct SymbolType
    {
        std::string name; // blank if a constant
        std::string val;
        bool isRealNum = false;
        double valAsReal = 0.0;
        bool isSetValueForRun = false;
        bool isParameter = false;
    };

    struct ParLogLineType
    {
        int statementKind = 0;
        int idfFieldNum = 0;
        int startIntCode = 0;
        int endIntCode = 0;
        int assignParam = 0;
        int symbolA = 0;
        int symbolB = 0;
        int objLine = 0;
    };

    struct StrucStackType
    {
        int mode = 0;
        std::string match; // for select
    };

    struct EvalStackType
    {
        std::string val;
        bool isRealNum = false;
        double valAsReal = 0.0;
    };

    // ---------------------------------------------------------------------------------
    // Routines called directly from the main sequence
    // ---------------------------------------------------------------------------------
    void openErrorFile();
    void openFilesFirstPass();
    void initialRead();
    void closeFilesFirstPass();
    void determineNumberOfCases();
    void setCases();
    void gatherParameterSymbols();
    void readSetValueObjects();
    void translateParametricLogic();
    void translateEmbeddedExpressions();
    void setValueForCase(int caseIn);
    void computeParametricLogic();
    void computeEmbeddedExpressions();
    void openFileSecondPass(int caseIn);
    void substituteValues();
    void closeFileSecondPass();
    void writeErrorsAndCloseFile();

    // ---------------------------------------------------------------------------------
    // Non-generic support routines
    // ---------------------------------------------------------------------------------
    void getNextDisabledObjectLines(int lineCountIn, int &foundObjectLinesOut, int &firstLineOut, int &lastLineOut);
    void getNextFoundExpression(int lineCountIn, int &foundExpressionOut, int &lineNumberOut);
    void evaluateExpression(int firstIntCodeIn, int lastIntCodeIn, std::string &resultStringOut, double &resultRealOut, bool &resultIsRealOut);
    void addToEvalStack();
    void pushSymbolOnEvalStack(int symbolRefIn);
    double popEvalReal();
    void pushEvalReal(double realIn);
    bool popEvalLogical();
    void pushEvalLogical(bool logicalIn);
    std::string popEvalString();
    bool isEvalStackTopReal(int numValsIn);
    static bool isResultTrue(std::string const &resultIn);
    void pushStrucStack(int modeIn, std::string const *matchIn = nullptr);
    int popStrucStack();
    int currentStrucStackTop() const;
    void replaceTopStrucStack(int modeIn);
    bool doesTopMatch(std::string const &argIn) const;
    int getObjectReference(int nameOfObjectSymbolRefIn, int kindOfObjectSymbolRefIn);
    void getConstants(std::string const &stringIn, int &aSymbolOut, int *bSymbolOut = nullptr);
    void expressionToRPN(std::string const &expressionIn, int &firstIntCodeOut, int &lastIntCodeOut);
    void getNextToken(std::string const &inString,
                      int endPrevToken,
                      int prevTokenClass,
                      int &startNewToken,
                      int &endNewToken,
                      int &newTokenClass);
    static void determineStatement(std::string const &inString, int &outKind, int &outAfter);
    void addIntCode(int intCodeIn);
    void addObjectLineReference(std::string const &objKind, std::string const &objName, int lineStart, int lineEnd);
    int lookupParameterSymbol(std::string const &symbolNameIn) const;
    void addNewObject(int kindOfObject);
    void addField(std::string const &fieldIn);
    bool addParameterSymbol(std::string const &parIn, bool ifSetValueForRunIn = false);
    void addConstantSymbol(std::string const &constantIn);
    void addConstantSymbol(std::string const &constantIn, double constantRealIn);
    int addNewExpression(std::string const &expString, int lineOfFile);
    void addToErrMsg(std::string const &textOfError, int kindOfError, int errorNumber);
    void pushOpStack(int opTok, int prec, int startFunc, int endFunc);
    void popOpStack(int &opTok, int &prec, int &startFunc, int &endFunc);
    int checkTopPrec() const;
    bool checkTopIsFunc() const;
    std::string opToken2String(int tokenIn);
    static int classifyPrecedence(int tokenIn);

    std::string intermediateFileName() const;

    // ---------------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------------
    bool m_verboseDebug = false;

    std::string m_inputFilePathName;
    std::string m_filePathOnly;
    std::string m_outputFileNameRoot;
    std::string m_outputFileName;
    std::string m_errorFileName;

    std::ifstream m_inFile;        // input IDF file
    std::ofstream m_tempFileOut;   // intermediate file (first pass, writing)
    std::ifstream m_tempFileIn;    // intermediate file (second pass, reading)
    std::ofstream m_outFile;       // output IDF file
    std::ofstream m_errFile;       // error file

    bool m_errorCondition = false;
    std::string m_errorContext;

    std::vector<ErrMsgType> m_errMsgs;
    Array1<FoundExpressionType> m_foundExpression;
    Array1<ObjectLinesType> m_objectLines;
    Array1<IdfObjectType> m_idfObject;
    Array1<std::string> m_idfField;
    Array1<CasesType> m_cases;
    int m_numCases = 0; // number of parametric cases - each case is an output file

    // valForRun[iSymbol][iCase] the values of the parameters in Symbol (both 1-based)
    std::vector<std::vector<std::string>> m_valForRun;

    // IntCode - Intermediate Code array used in Parametric:Logic and expressions in
    // other objects. Ranges of values have specific meanings:
    //   -1 to -127 are token constants
    //   1 to numSymbol are references to the Symbol table
    // The IntCode is a list of codes evaluated in a stack based (reverse polish notation)
    // approach with statements and tokens acting as operators and references to the symbol
    // table acting as operands.
    Array1<int> m_intCode;

    Array1<SymbolType> m_symbol;
    int m_lastSymbolSetValueForRun = 0;
    int m_lastSymbolParameter = 0;

    Array1<ParLogLineType> m_parLogLine;
    bool m_parLogLineAllocated = false;

    // Stacks keep their slots between uses (like the Fortran arrays they replace) and only
    // the "top" index moves.
    Array1<OpStackType> m_opStack;
    int m_opStackTop = 0;
    Array1<StrucStackType> m_strucStack;
    int m_topStrucStack = 0;
    Array1<EvalStackType> m_evalStack;
    int m_topEvalStack = 0;

    // saved positions for getNextDisabledObjectLines and getNextFoundExpression
    int m_indexObjectLines = 1;
    int m_indexFoundExpression = 1;
};

// Generic (not program specific) support routines, exposed for testing.
std::string pathOnly(std::string const &fileWithPathIn);
std::string noExtension(std::string const &stringIn);
double stringToReal(std::string const &stringIn);
std::string intToStr6(int intIn);
std::string realToString(double realIn);
bool isStrEq(std::string const &arg1, std::string const &arg2);

} // namespace ParametricPreprocessor

#endif
