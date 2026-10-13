// ParametricPreprocessor - C++ port of the EnergyPlus Fortran ParametricPreprocessor.
// See ParametricPreprocessor.hh for a description of the program.

#include "ParametricPreprocessor.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace ParametricPreprocessor {

namespace {

    // character classes
    constexpr int charNum = 1;    // 0123456789
    constexpr int charPeriod = 2; // .
    constexpr int charE = 3;      // e and E (and d and D)
    constexpr int charAZ = 4;     // letters except for e, E, d and D
    constexpr int charPlus = 5;
    constexpr int charMinus = 6;
    constexpr int charMult = 7;
    constexpr int charDiv = 8;
    constexpr int charExp = 9;
    constexpr int charLeftParen = 10;
    constexpr int charRightParen = 11;
    constexpr int charDoubleQuote = 12;
    constexpr int charEqual = 13;
    constexpr int charGreat = 14;
    constexpr int charLess = 15;
    constexpr int charTilde = 16;
    constexpr int charSpace = 17;
    constexpr int charDollar = 18;
    constexpr int charAmpersand = 19;
    constexpr int charPipe = 20;
    constexpr int charUnderscore = 21;
    constexpr int charOther = 100;
    constexpr int charNone = 200;

    // tokens
    constexpr int tokNum = -1;  // numbers, can start with minus, one period, one E or e   -0.343E-01
    constexpr int tokStr = -2;  // strings starting and ending with "                      "string with spaces"
    constexpr int tokID = -3;   // name containing letters only starting with $            $HEIGHT
    constexpr int tokFunc = -4;
    constexpr int tokPlus = -5;
    constexpr int tokMinus = -6;
    constexpr int tokTimes = -7;
    constexpr int tokDiv = -8;
    constexpr int tokExp = -9;
    constexpr int tokRtParen = -10;
    constexpr int tokLtParen = -11;
    constexpr int tokGT = -12;
    constexpr int tokEQ = -13; // ==
    constexpr int tokLT = -14;
    constexpr int tokGE = -15; // >= or =>
    constexpr int tokLE = -16; // <= or =<
    constexpr int tokNE = -17;
    constexpr int tokAnd = -18;   // &&
    constexpr int tokOr = -19;    // ||
    constexpr int tokTilde = -20; // used a separator for functions since comma cannot be used
    constexpr int tokUnNeg = -21; // unary negation -$PAR for example
    constexpr int tokFuncABS = -22;
    constexpr int tokFuncACOS = -23;
    constexpr int tokFuncASIN = -24;
    constexpr int tokFuncATAN = -25;
    constexpr int tokFuncCOS = -26;
    constexpr int tokFuncEXP = -27;
    constexpr int tokFuncINT = -28;
    constexpr int tokFuncLEN = -29;
    constexpr int tokFuncLOG = -30;
    constexpr int tokFuncMOD = -31;
    constexpr int tokFuncNOT = -32;
    constexpr int tokFuncSIN = -33;
    constexpr int tokFuncSQRT = -34;
    constexpr int tokFuncTAN = -35;
    constexpr int tokNone = -98;
    constexpr int tokINVALID = -99;

    // operator precedence
    constexpr int precExp = 6;
    constexpr int precMultDiv = 5;
    constexpr int precAddSub = 4;
    constexpr int precCompare = 3;
    constexpr int precAndOr = 2;
    constexpr int precLeftParen = 1;
    constexpr int precNoItem = 0;

    // message kinds
    constexpr int msgError = 1;
    constexpr int msgWarning = 2;
    constexpr std::size_t maxErrorLength = 300;

    constexpr int reset = -999;

    // kinds of Parametric:* objects
    constexpr int koSetValueForRun = 1;
    constexpr int koLogic = 2;
    constexpr int koRunControl = 3;
    constexpr int koFileNameSuffix = 4;

    // kind of statement
    constexpr int ksNone = -128;
    constexpr int ksAssignment = -129;
    constexpr int ksParameter = -130;
    constexpr int ksIf = -131;
    constexpr int ksElseIf = -132;
    constexpr int ksElse = -133;
    constexpr int ksEndIf = -134;
    constexpr int ksSelect = -135;
    constexpr int ksCase = -136;
    constexpr int ksDefault = -137;
    constexpr int ksEndSelect = -138;
    constexpr int ksDisable = -139;
    constexpr int ksEnable = -140;
    constexpr int ksRemark = -141;

    // structure stack modes
    constexpr int ssmNotInStruc = 1;      // not within an IF or SELECT structure
    constexpr int ssmDoThen = 2;          // within the THEN block because IF was true statement is true
    constexpr int ssmSkipToElse = 3;      // skip to the ELSE or ELSEIF because IF was false or ELSEIF was false
    constexpr int ssmDoElse = 4;          // within the ELSE block looking for the ENDIF
    constexpr int ssmSkipToEndIf = 5;     // within part of the IF structure not being executed looking for the ENDIF
    constexpr int ssmFindCase = 6;        // looking for matching CASE value within SELECT structure
    constexpr int ssmDoCase = 7;          // executing a matching CASE block within SELECT structure
    constexpr int ssmSkipToEndSelect = 8; // within part of the SELECT structure not being executed looking for the ENDSELECT

    // ---------------------------------------------------------------------------------
    // String helpers that reproduce Fortran fixed-length (blank padded) string semantics.
    // Trailing blanks are never significant in Fortran comparisons, so strings are kept
    // right-trimmed where the original relied on blank padding.
    // ---------------------------------------------------------------------------------

    // LEN_TRIM
    int lenTrim(std::string const &s)
    {
        std::size_t const pos = s.find_last_not_of(' ');
        return pos == std::string::npos ? 0 : static_cast<int>(pos) + 1;
    }

    // TRIM
    std::string rtrim(std::string const &s)
    {
        return s.substr(0, static_cast<std::size_t>(lenTrim(s)));
    }

    // ADJUSTL (followed by an implied TRIM)
    std::string adjustl(std::string const &s)
    {
        std::size_t const first = s.find_first_not_of(' ');
        if (first == std::string::npos) return std::string();
        return rtrim(s.substr(first));
    }

    // s(first:last) using 1-based inclusive positions; positions past the end are blanks
    // which are dropped since they are never significant.
    std::string sub(std::string const &s, int first, int last)
    {
        if (first < 1) first = 1;
        if (last > static_cast<int>(s.size())) last = static_cast<int>(s.size());
        if (last < first) return std::string();
        return s.substr(static_cast<std::size_t>(first - 1), static_cast<std::size_t>(last - first + 1));
    }

    // s(first:)
    std::string subFrom(std::string const &s, int first)
    {
        return sub(s, first, static_cast<int>(s.size()));
    }

    // 1-based character access with blank padding
    char charAt(std::string const &s, int pos)
    {
        if (pos < 1 || pos > static_cast<int>(s.size())) return ' ';
        return s[static_cast<std::size_t>(pos - 1)];
    }

    // INDEX(s, target) - 1-based position or zero if not found
    int indexOf(std::string const &s, std::string const &target)
    {
        std::size_t const pos = s.find(target);
        return pos == std::string::npos ? 0 : static_cast<int>(pos) + 1;
    }

    // INDEX(s, target, BACK=.TRUE.)
    int indexOfBack(std::string const &s, char target)
    {
        std::size_t const pos = s.rfind(target);
        return pos == std::string::npos ? 0 : static_cast<int>(pos) + 1;
    }

    // VERIFY(TRIM(s), set) == 0  -> every character of s is in set
    bool allCharsIn(std::string const &s, char const *set)
    {
        return rtrim(s).find_first_not_of(set) == std::string::npos;
    }

    // Fortran character relational comparison: the shorter operand is padded with blanks.
    int fortranCompare(std::string const &a, std::string const &b)
    {
        std::size_t const n = std::max(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            auto const ca = static_cast<unsigned char>(i < a.size() ? a[i] : ' ');
            auto const cb = static_cast<unsigned char>(i < b.size() ? b[i] : ' ');
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        return 0;
    }

    // Fortran I4 edit descriptor
    std::string formatI4(int value)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%4d", value);
        std::string result(buf);
        if (result.size() > 4) return "****";
        return result;
    }

    // Fortran I6 edit descriptor
    std::string formatI6(int value)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%6d", value);
        std::string result(buf);
        if (result.size() > 6) return "******";
        return result;
    }

    // Read one record from a formatted sequential file, removing a carriage return left
    // over from Windows style line endings.
    bool readLine(std::istream &stream, std::string &line)
    {
        if (!std::getline(stream, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    }

    void replaceTabs(std::string &line)
    {
        std::replace(line.begin(), line.end(), '\t', ' ');
    }

    bool fileExists(std::string const &name)
    {
        if (name.empty()) return false;
        std::ifstream test(name);
        return test.good();
    }

    // Classify the given character into a category (see character class constants)
    int classifyChar(char singleChar)
    {
        int const c = static_cast<unsigned char>(singleChar);
        if (c >= 48 && c <= 57) return charNum; // 0123456789
        switch (c) {
        case 46: return charPeriod; // .
        case 68:
        case 69:
        case 100:
        case 101: return charE; // d D e E
        case 43: return charPlus;
        case 45: return charMinus;
        case 42: return charMult;
        case 47: return charDiv;
        case 94: return charExp;
        case 40: return charLeftParen;
        case 41: return charRightParen;
        case 34: return charDoubleQuote;
        case 61: return charEqual;
        case 62: return charGreat;
        case 60: return charLess;
        case 126: return charTilde;
        case 32: return charSpace;
        case 36: return charDollar;
        case 38: return charAmpersand;
        case 95: return charUnderscore;
        case 124: return charPipe;
        default: break;
        }
        if ((c >= 65 && c <= 90) || (c >= 97 && c <= 122)) return charAZ; // letters other than d D e E
        return charOther;
    }

} // namespace

//=====================================================================================
//
//    GENERIC (NOT PROGRAM SPECIFIC) SUPPORT ROUTINES
//
//=====================================================================================

// Return the string that only includes only the path including the trailing slash
std::string pathOnly(std::string const &fileWithPathIn)
{
    // first look for DOS/Windows style slash
    int slashPos = indexOfBack(fileWithPathIn, '\\');
    // now look for Unix style slash
    if (slashPos == 0) slashPos = indexOfBack(fileWithPathIn, '/');
    if (slashPos >= 1) return fileWithPathIn.substr(0, static_cast<std::size_t>(slashPos));
    return std::string();
}

// Remove the file extension
std::string noExtension(std::string const &stringIn)
{
    int const dotPos = indexOfBack(stringIn, '.');
    if (dotPos >= 1) return stringIn.substr(0, static_cast<std::size_t>(dotPos - 1));
    return stringIn;
}

// Convert from a string to a real
double stringToReal(std::string const &stringIn)
{
    std::string const trimmed = adjustl(stringIn);
    if (trimmed.empty()) return 0.0;
    if (!allCharsIn(stringIn, "-0123456789.E+")) return 0.0;
    // The Fortran version used a list-directed READ which stops the program on a malformed
    // number; here a malformed number is treated as zero.
    char *end = nullptr;
    double const value = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0') return 0.0;
    return value;
}

// Convert an integer value to a string in the form of 000000 to 999999
std::string intToStr6(int intIn)
{
    if (intIn <= 999999 && intIn >= 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%06d", intIn);
        return buf;
    }
    return "Invali"; // FORTRAN-COMPAT: 'Invalid' assigned to a CHARACTER(len=6) result
}

// Convert a real to a string using the Fortran edit descriptors F13.6 or E18.6E3
std::string realToString(double realIn)
{
    // FORTRAN-COMPAT: the original compared against the default (single precision) real
    // literal 0.00001
    double const smallLimit = static_cast<double>(0.00001f);
    bool const useExponent = (realIn < smallLimit && realIn > -smallLimit) || (realIn > 100000.0 || realIn < -100000.0);
    if (std::isnan(realIn)) return "NaN";
    if (std::isinf(realIn)) return realIn > 0 ? "Infinity" : "-Infinity";
    char buf[64];
    if (!useExponent) {
        std::snprintf(buf, sizeof(buf), "%.6f", realIn);
        std::string result(buf);
        // a value that does not fit the field width is shown as asterisks (e.g. exactly -100000)
        if (result.size() > 13) return std::string(13, '*');
        return result;
    }
    // Fortran E editing produces a mantissa of the form 0.dddddd
    std::string const sign = std::signbit(realIn) ? "-" : "";
    if (realIn == 0.0) return sign + "0.000000E+000";
    std::snprintf(buf, sizeof(buf), "%.5e", std::fabs(realIn)); // d.ddddde+XX
    std::string const formatted(buf);
    std::size_t const ePos = formatted.find('e');
    std::string const digits = formatted.substr(0, 1) + formatted.substr(2, ePos - 2);
    int const exponent = std::atoi(formatted.c_str() + ePos + 1) + 1;
    std::snprintf(buf, sizeof(buf), "%c%03d", exponent < 0 ? '-' : '+', std::abs(exponent));
    return sign + "0." + digits + "E" + buf;
}

// Compare two strings and ignore case (trailing blanks are ignored)
bool isStrEq(std::string const &arg1, std::string const &arg2)
{
    int const lenArg1 = lenTrim(arg1);
    int const lenArg2 = lenTrim(arg2);
    if (lenArg1 != lenArg2) return false;
    for (int i = 0; i < lenArg1; ++i) {
        int const c1 = static_cast<unsigned char>(arg1[static_cast<std::size_t>(i)]);
        int const c2 = static_cast<unsigned char>(arg2[static_cast<std::size_t>(i)]);
        if (c1 == c2) continue;
        if (c1 >= 65 && c1 <= 90) { // uppercase
            if (c1 + 32 != c2) return false;
        } else if (c1 >= 97 && c1 <= 122) { // lowercase
            if (c1 - 32 != c2) return false;
        } else {
            return false;
        }
    }
    return true;
}

//=====================================================================================
//
//    MAIN SEQUENCE
//
//=====================================================================================

Preprocessor::Preprocessor(bool verboseDebug) : m_verboseDebug(verboseDebug) {}

int Preprocessor::run(std::string const &inputFilePathName)
{
    m_inputFilePathName = inputFilePathName;
    if (m_verboseDebug && lenTrim(m_inputFilePathName) == 0) {
        std::cout << " InputFilePathName:" << rtrim(m_inputFilePathName) << '\n';
    }
    m_filePathOnly = pathOnly(m_inputFilePathName);
    m_outputFileNameRoot = noExtension(m_inputFilePathName);
    openErrorFile();
    openFilesFirstPass();
    initialRead(); // read IDF and create intermediate and read parametric objects and fields
    closeFilesFirstPass();
    determineNumberOfCases();
    m_cases.resize(m_numCases);
    setCases(); // interpret RunControl and FileNameSuffix
    gatherParameterSymbols(); // make list of all params
    m_valForRun.assign(static_cast<std::size_t>(m_lastSymbolParameter) + 1,
                       std::vector<std::string>(static_cast<std::size_t>(m_numCases) + 1));
    readSetValueObjects();
    translateParametricLogic();
    translateEmbeddedExpressions();
    for (int iCases = 1; iCases <= m_numCases; ++iCases) {
        if (m_cases(iCases).active) {
            setValueForCase(iCases);
            computeParametricLogic();
            computeEmbeddedExpressions();
            openFileSecondPass(iCases);
            substituteValues(); // substitute values into intermediate files for all output files
            closeFileSecondPass();
        }
    }
    writeErrorsAndCloseFile();
    return numErrMsgs();
}

std::string Preprocessor::intermediateFileName() const
{
    return m_filePathOnly + "parametric.int";
}

//----------------------------------------------------------------------------------
// Open the error file.
//----------------------------------------------------------------------------------
void Preprocessor::openErrorFile()
{
    if (m_verboseDebug) std::cout << " Started OpenErrorFile\n";
    m_errorFileName = rtrim(m_outputFileNameRoot) + ".err";
    m_errFile.open(m_errorFileName, std::ios::out | std::ios::trunc);
    if (!m_errFile.is_open()) std::cout << "Cannot open error file.\n";
}

//----------------------------------------------------------------------------------
// Open the IDF file for reading and the intermediate file for writing
//----------------------------------------------------------------------------------
void Preprocessor::openFilesFirstPass()
{
    if (m_verboseDebug) std::cout << " Started OpenFilesFirstPass\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    if (fileExists(m_inputFilePathName)) {
        m_inFile.open(m_inputFilePathName);
        if (!m_inFile.is_open()) addToErrMsg("error opening idf file", msgError, 0);
        m_tempFileOut.open(intermediateFileName(), std::ios::out | std::ios::trunc);
        if (!m_tempFileOut.is_open()) addToErrMsg("error opening intermediate file named parametric.int", msgError, 0);
    } else {
        addToErrMsg("error opening idf file - no file found", msgError, 0);
    }
}

//----------------------------------------------------------------------------------
// Read IDF and create intermediate and read parametric objects and fields.
// The intermediate file will contain the same items as the IDF file but
// also includes references to expressions instead of the expressions
// themselves. Each expression is replaces with =@@000001 where the =@@
// is an indicator that the following number is a reference for an expression.
// The 000001 is a index for the array of expressions and will have the substituted
// value.
//
// The objects:
//        Parametric:SetValueForRun
//        Parametric:Logic
//        Parametric:RunControl
//        Parametric:FileNameSuffix
// are extracted from the IDF file and not written to the intermediate file.
//
// In addition, the object type and first field (usually the name of the object)
// along with the first line and last line number of the object are stored for
// possible disabling.
//----------------------------------------------------------------------------------
void Preprocessor::initialRead()
{
    constexpr int omOut = 1;   // objMode is out of any object
    constexpr int omNorm = 2;  // objMode is in normal object
    constexpr int omParam = 3; // objMode is in parametric object
    constexpr int fmOut = 1;   // fldMode is out of any field
    constexpr int fmCmt = 2;   // fldMode is in a comment
    constexpr int fmNorm = 3;  // fldMode is in a normal field
    constexpr int fmExpr = 4;  // fldMode is in an expression field, starting with =$

    if (m_verboseDebug) std::cout << " Started InitialRead\n";
    if (m_errorCondition) return; // if an error exists do not run this routine

    std::string lineIn;
    std::string lineOut;
    std::string curField;
    std::string kindOfObj;
    std::string curNameOfObj;
    int objMode = omOut;
    int fldMode = fmOut;
    int fldCount = 0;
    int lineCount = 1;
    int startOfField = 0;
    int lineAtObjStart = 0;

    auto appendExpressionReference = [&](int lineNumber) {
        // the format will be =@@123456 which is always nine characters
        int const curExpressionID = addNewExpression(subFrom(curField, 2), lineNumber);
        lineOut += "=@@" + intToStr6(curExpressionID);
    };

    while (readLine(m_inFile, lineIn)) {
        replaceTabs(lineIn);
        int const lineInLen = lenTrim(lineIn);
        lineOut.clear();
        bool doOutputLine = true;
        fldMode = fmOut;
        for (int lineInPos = 1; lineInPos <= lineInLen; ++lineInPos) {
            bool doOutputChar = true;
            char const curChar = lineIn[static_cast<std::size_t>(lineInPos - 1)];
            switch (curChar) {
            case ',':
                if (fldMode == fmNorm || fldMode == fmExpr) {
                    curField = sub(lineIn, startOfField, lineInPos - 1);
                    ++fldCount;
                }
                if (fldMode == fmNorm) {
                    fldMode = fmOut;
                    if (fldCount == 1) { // first field
                        kindOfObj = rtrim(curField);
                        if (isStrEq(kindOfObj, "Parametric:SetValueForRun")) {
                            addNewObject(koSetValueForRun);
                            objMode = omParam;
                        } else if (isStrEq(kindOfObj, "Parametric:Logic")) {
                            addNewObject(koLogic);
                            objMode = omParam;
                        } else if (isStrEq(kindOfObj, "Parametric:RunControl")) {
                            addNewObject(koRunControl);
                            objMode = omParam;
                        } else if (isStrEq(kindOfObj, "Parametric:FileNameSuffix")) {
                            addNewObject(koFileNameSuffix);
                            objMode = omParam;
                        } else {
                            objMode = omNorm;
                            lineAtObjStart = lineCount;
                        }
                    } else if (fldCount == 2) { // second field
                        curNameOfObj = rtrim(curField);
                        if (objMode == omParam) addField(curField);
                    } else { // all other fields
                        if (objMode == omParam) addField(curField);
                    }
                    if (objMode == omParam) doOutputLine = false;
                } else if (fldMode == fmExpr) {
                    fldMode = fmOut;
                    objMode = omNorm;
                    appendExpressionReference(lineCount);
                }
                break;
            case ';':
                if (fldMode == fmNorm || fldMode == fmExpr) {
                    curField = sub(lineIn, startOfField, lineInPos - 1);
                    ++fldCount;
                } else if (fldMode == fmOut) { // if the last field is blank (just a semicolon) process it (fix related to CR8536)
                    curField.clear();
                    ++fldCount;
                    fldMode = fmNorm;
                }
                // FORTRAN-COMPAT: also done when the semicolon is within a comment
                if (objMode == omParam) addField(curField);
                if (fldMode == fmNorm) {
                    if (objMode == omParam) doOutputLine = false;
                    fldMode = fmOut;
                    objMode = omOut;
                    addObjectLineReference(kindOfObj, curNameOfObj, lineAtObjStart, lineCount);
                    fldCount = 0;
                } else if (fldMode == fmExpr) {
                    fldMode = fmOut;
                    objMode = omOut;
                    appendExpressionReference(lineCount);
                    fldCount = 0;
                }
                break;
            case '!':
                fldMode = fmCmt;
                break;
            case '=':
                if (fldMode == fmOut && objMode == omNorm) {
                    fldMode = fmExpr;
                    startOfField = lineInPos;
                    doOutputChar = false;
                }
                break;
            case ' ':
                // do nothing
                break;
            default:
                if (fldMode == fmOut) {
                    fldMode = fmNorm;
                    startOfField = lineInPos;
                } else if (fldMode == fmExpr) {
                    doOutputChar = false;
                }
                break;
            }
            if (doOutputChar) lineOut += curChar;
        }
        if (doOutputLine) {
            ++lineCount;
            m_tempFileOut << rtrim(lineOut) << '\n';
        }
    }
}

//----------------------------------------------------------------------------------
// Close the IDF file and the intermediate file
//----------------------------------------------------------------------------------
void Preprocessor::closeFilesFirstPass()
{
    if (m_verboseDebug) std::cout << " Started CloseFilesFirstPass\n";
    if (m_inFile.is_open()) m_inFile.close();
    if (m_tempFileOut.is_open()) m_tempFileOut.close();
}

//----------------------------------------------------------------------------------
// Determine the number of parametric cases
//----------------------------------------------------------------------------------
void Preprocessor::determineNumberOfCases()
{
    if (m_verboseDebug) std::cout << " Started DetermineNumberOfCases\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    m_numCases = 0;
    for (int iObj = 1; iObj <= m_idfObject.size(); ++iObj) {
        IdfObjectType const &obj = m_idfObject(iObj);
        int const curNumFields = (obj.lastField - obj.firstField) + 1;
        int const curNumPossCases = curNumFields - 1;
        switch (obj.kindObj) {
        case koSetValueForRun:
        case koRunControl:
        case koFileNameSuffix:
            if (curNumPossCases > m_numCases) m_numCases = curNumPossCases;
            break;
        case koLogic: // if ParametricLogic is present make at least one case
            if (m_numCases == 0) m_numCases = 1;
            break;
        default: break;
        }
    }
    // if just embedded expressions than make at least one case.
    if (m_numCases == 0 && m_foundExpression.size() >= 1) m_numCases = 1;
}

//----------------------------------------------------------------------------------
// Interpret the Parametric:RunControl and Parametric:FileNameSuffix
//----------------------------------------------------------------------------------
void Preprocessor::setCases()
{
    if (m_verboseDebug) std::cout << " Started SetCases\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    bool foundRunControl = false;
    bool foundFileNameSuffix = false;
    int const numIdfField = m_idfField.size();
    // set the default values for Case
    for (int kPar = 1; kPar <= m_numCases; ++kPar) {
        m_cases(kPar).active = true;
        m_cases(kPar).suffix = intToStr6(kPar); // always six characters long
    }
    for (int iObj = 1; iObj <= m_idfObject.size(); ++iObj) {
        IdfObjectType const &obj = m_idfObject(iObj);
        int const numCurFields = (obj.lastField - obj.firstField) + 1;
        if (obj.kindObj == koRunControl) {
            if (!foundRunControl) {
                foundRunControl = true; // set flag in case a second object is found
                for (int jFld = 2; jFld <= numCurFields; ++jFld) { // ignore the name of the object so skip first field
                    int const curField = obj.firstField + jFld - 1;
                    if (curField > 0 && curField <= numIdfField) {
                        if (isStrEq(m_idfField(curField), "yes")) {
                            m_cases(jFld - 1).active = true;
                        } else if (isStrEq(m_idfField(curField), "no")) {
                            m_cases(jFld - 1).active = false;
                        } else {
                            addToErrMsg("Invalid field in Parametric:RunControl" + rtrim(m_idfField(curField)), msgError, 0);
                        }
                    }
                }
            } else {
                addToErrMsg("More than one Parametric:RunControl object found in file.", msgError, 0);
            }
        } else if (obj.kindObj == koFileNameSuffix) {
            if (!foundFileNameSuffix) {
                foundFileNameSuffix = true; // set flag in case a second object is found
                for (int jFld = 2; jFld <= numCurFields; ++jFld) { // ignore the name of the object so skip first field
                    int const curField = obj.firstField + jFld - 1;
                    if (curField > 0 && curField <= numIdfField) {
                        m_cases(jFld - 1).suffix = m_idfField(curField);
                    }
                }
            } else {
                addToErrMsg("More than one Parametric:FileNameSuffix object found in file.", msgError, 0);
            }
        }
    }
    // Test for duplicates
    for (int kPar = 1; kPar <= m_numCases - 1; ++kPar) {
        if (!m_cases(kPar).active) continue;
        if (m_cases(kPar).dup2) continue;
        for (int jPar = kPar + 1; jPar <= m_numCases; ++jPar) {
            if (fortranCompare(m_cases(kPar).suffix, m_cases(jPar).suffix) == 0) {
                m_cases(kPar).dup = true;
                m_cases(jPar).dup2 = true;
            }
        }
    }
    int dupcount = 0;
    for (int kPar = 1; kPar <= m_numCases; ++kPar) {
        if (m_cases(kPar).dup) ++dupcount;
    }
    if (dupcount > 0) {
        m_errorContext = "Parametric:FileNameSuffix";
        addToErrMsg("Duplicate names entered in suffix list. Cannot have duplicate names.", msgError, dupcount);
        if (m_verboseDebug) {
            std::cout << " Duplicate suffixes:\n";
            for (int kPar = 1; kPar <= m_numCases; ++kPar) {
                if (!m_cases(kPar).dup) continue;
                std::cout << "  suffix# " << formatI6(kPar) << ' ' << rtrim(m_cases(kPar).suffix) << '\n';
            }
        }
    }
}

//----------------------------------------------------------------------------------
// Scan the Parametric:Logic and Parametric:SetValueForRun for parameters
// in the form of $AAA
//----------------------------------------------------------------------------------
void Preprocessor::gatherParameterSymbols()
{
    if (m_verboseDebug) std::cout << " Started GatherParameterSymbols\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    int const numIdfField = m_idfField.size();
    for (int iObj = 1; iObj <= m_idfObject.size(); ++iObj) {
        IdfObjectType const &obj = m_idfObject(iObj);
        if (obj.kindObj == koSetValueForRun) {
            int const curField = obj.firstField;
            if (curField > 0 && curField <= numIdfField) {
                bool const curErr = addParameterSymbol(m_idfField(curField), true); // create symbol and link to SetValueForRun
                if (m_verboseDebug && curErr) std::cout << "GatherParameterSymbolsA: " << m_symbol(m_symbol.size()).name << '\n';
                if (!curErr) {
                    addToErrMsg("Parameter name contains invalid characters. " + rtrim(m_idfField(curField)), msgError, 0);
                }
            }
        } else if (obj.kindObj == koLogic) {
            // scan for PARAMETER statements
            if (obj.lastField > obj.firstField) {
                for (int jFld = obj.firstField + 1; jFld <= obj.lastField; ++jFld) {
                    std::string const curFldVal = adjustl(m_idfField(jFld));
                    if (isStrEq(sub(curFldVal, 1, 10), "PARAMETER ")) {
                        bool const curErr = addParameterSymbol(subFrom(curFldVal, 11));
                        if (!curErr) {
                            addToErrMsg("Parameter name contains invalid characters. " + curFldVal + " in " +
                                            rtrim(m_idfField(obj.firstField)),
                                        msgError,
                                        0);
                        }
                    }
                }
            }
        }
    }
}

//----------------------------------------------------------------------------------
// Interpret the Parametric:SetValuesForRun
//----------------------------------------------------------------------------------
void Preprocessor::readSetValueObjects()
{
    if (m_verboseDebug) std::cout << " Started ReadSetValueObjects\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    int const numIdfField = m_idfField.size();
    for (int iObj = 1; iObj <= m_idfObject.size(); ++iObj) {
        IdfObjectType const &obj = m_idfObject(iObj);
        if (obj.kindObj != koSetValueForRun) continue;
        int const nameField = obj.firstField;
        if (nameField <= 0 || nameField > numIdfField) continue;
        int const curParam = lookupParameterSymbol(m_idfField(nameField));
        if (curParam > 0) {
            if (curParam <= m_lastSymbolSetValueForRun) {
                int const numFld = obj.lastField - obj.firstField;
                if (numFld <= m_numCases) {
                    auto &paramValues = m_valForRun.at(static_cast<std::size_t>(curParam));
                    // transfer the field value into the parameter value array
                    for (int jCase = 1; jCase <= numFld; ++jCase) {
                        paramValues.at(static_cast<std::size_t>(jCase)) = m_idfField(nameField + jCase);
                    }
                    // if not enough items, fill the array with the last item
                    for (int jCase = numFld + 1; jCase <= m_numCases; ++jCase) {
                        paramValues.at(static_cast<std::size_t>(jCase)) = m_idfField(nameField + numFld);
                    }
                }
            } else {
                addToErrMsg("SetParameterForRun must contain at least one value. " + rtrim(m_idfField(nameField)), msgError, 0);
            }
        } else {
            addToErrMsg("Invalid parameter name. " + rtrim(m_idfField(nameField)), msgError, 0);
        }
    }
}

//----------------------------------------------------------------------------------
// Translate the "programming language" present in the Parametric:Logic object
// into intermediate code and a symbol table.  This is like compiling a programming
// language into a machine code but instead the code is a simple list that appears
// in the intermediate code array, IntCode.
//
// The possible lines include:
//   <id> = <expression>                        assignment
//   PARAMETER <id>                             define parameter
//   IF <expresson>
//   ELSEIF <expression>
//   ELSE
//   ENDIF
//   SELECT <expression>
//   CASE <constant>
//   DEFAULT                                    if no constant matches
//   ENDSELECT
//   ENABLE <constant>                          the constant is the object name
//   ENABLE <constant> <constant>               object name and object type
//   DISABLE <constant>                         the constant is the object name
//   DISABLE <constant> <constant>              object name and object type
//   REMARK <text to ignore>
//
// The logic is applied to each case on a line by line basis.
//----------------------------------------------------------------------------------
void Preprocessor::translateParametricLogic()
{
    if (m_verboseDebug) std::cout << " Started TranslateParametricLogic\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    bool foundParametricLogic = false;
    // find the Parametric:Logic object
    for (int iObj = 1; iObj <= m_idfObject.size(); ++iObj) {
        IdfObjectType const obj = m_idfObject(iObj);
        if (obj.kindObj != koLogic) continue;
        if (foundParametricLogic) {
            addToErrMsg("Only one Parametric:Logic object can be used.", msgError, 0);
            continue;
        }
        foundParametricLogic = true; // set flag in case a second object is found
        int const numParLogLine = obj.lastField - obj.firstField;
        m_parLogLine.clear();
        m_parLogLine.resize(std::max(numParLogLine, 0));
        m_parLogLineAllocated = true;
        for (int jLine = 1; jLine <= numParLogLine; ++jLine) {
            std::string const curLine = m_idfField(obj.firstField + jLine);
            ParLogLineType &parLogLine = m_parLogLine(jLine);
            parLogLine.idfFieldNum = obj.firstField + jLine;
            int statementKind = 0;
            int afterCommand = 0;
            determineStatement(curLine, statementKind, afterCommand);
            parLogLine.statementKind = statementKind;
            std::string const curExpression = subFrom(curLine, afterCommand + 1);
            bool const blankAfterStatement = lenTrim(curExpression) == 0;
            switch (statementKind) {
            case ksNone: addToErrMsg("Invalid statement. Line ignored: " + rtrim(curLine), msgWarning, 0); break;
            case ksAssignment: {
                // for assignments the afterCommand should be pointing to the equals sign
                int const assignedParameter = lookupParameterSymbol(sub(curLine, 1, afterCommand - 1));
                if (assignedParameter >= 1) {
                    parLogLine.assignParam = assignedParameter;
                    expressionToRPN(curExpression, parLogLine.startIntCode, parLogLine.endIntCode);
                } else {
                    addToErrMsg("Parameter being assigned was not found. Line ignored: " + rtrim(curLine), msgWarning, 0);
                }
                break;
            }
            case ksParameter:
                // do nothing - these were scanned during the GatherParameterSymbols routine
                break;
            case ksIf:
            case ksElseIf:
            case ksSelect: expressionToRPN(curExpression, parLogLine.startIntCode, parLogLine.endIntCode); break;
            case ksElse:
            case ksEndIf:
            case ksDefault:
            case ksEndSelect:
                if (!blankAfterStatement) {
                    addToErrMsg("No text should appear after the statement. The extra text is ignored: " + rtrim(curLine), msgWarning, 0);
                }
                break;
            case ksCase:
                getConstants(curExpression, parLogLine.symbolA);
                if (parLogLine.symbolA == 0) {
                    addToErrMsg("Invalid expression - a constant was expected. The line is ignored: " + rtrim(curLine), msgWarning, 0);
                }
                break;
            case ksDisable:
            case ksEnable:
                getConstants(curExpression, parLogLine.symbolA, &parLogLine.symbolB);
                if (parLogLine.symbolA == 0) {
                    addToErrMsg("Invalid - an object name was expected. The line is ignored: " + rtrim(curLine), msgWarning, 0);
                }
                parLogLine.objLine = getObjectReference(parLogLine.symbolA, parLogLine.symbolB);
                break;
            case ksRemark:
                // do nothing - the remaining text is a remark
                break;
            default: break;
            }
        }
    }
}

//----------------------------------------------------------------------------------
// Translate all expressions that are embedded in objects
//----------------------------------------------------------------------------------
void Preprocessor::translateEmbeddedExpressions()
{
    if (m_verboseDebug) std::cout << " Started TranslateEmbeddedExpressions\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    for (int iFndExp = 1; iFndExp <= m_foundExpression.size(); ++iFndExp) {
        std::string const text = m_foundExpression(iFndExp).text;
        int startIntCode = 0;
        int endIntCode = 0;
        expressionToRPN(text, startIntCode, endIntCode);
        m_foundExpression(iFndExp).startIntCode = startIntCode;
        m_foundExpression(iFndExp).endIntCode = endIntCode;
    }
}

//----------------------------------------------------------------------------------
// Set the values from Parametric:SetValuesForRun to specific value for the specific
// run (or case)
//----------------------------------------------------------------------------------
void Preprocessor::setValueForCase(int caseIn)
{
    if (m_verboseDebug) std::cout << " Started SetValueForCase: " << formatI4(caseIn) << '\n';
    if (m_errorCondition) return; // if an error exists do not run this routine
    for (int iSym = 1; iSym <= m_lastSymbolParameter; ++iSym) {
        std::string const &value = m_valForRun.at(static_cast<std::size_t>(iSym)).at(static_cast<std::size_t>(caseIn));
        SymbolType &sym = m_symbol(iSym);
        sym.val = value;
        if (allCharsIn(value, "-0123456789.DE+")) {
            sym.isRealNum = true;
            sym.valAsReal = stringToReal(value);
        } else {
            sym.isRealNum = false;
        }
    }
}

//----------------------------------------------------------------------------------
// Execute the intermediate code for each line of the Parametric:Logic lines.
//----------------------------------------------------------------------------------
void Preprocessor::computeParametricLogic()
{
    if (m_verboseDebug) std::cout << " Started ComputeParametricLogic\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    if (!m_parLogLineAllocated) return;
    std::string resultString;
    double resultReal = 0.0;
    bool resultIsReal = false;
    // reset all object lines to be included
    for (int jObjLines = 1; jObjLines <= m_objectLines.size(); ++jObjLines) {
        m_objectLines(jObjLines).enabled = true;
    }
    // FORTRAN-COMPAT: the structure stack is never emptied between cases
    pushStrucStack(ssmNotInStruc);
    for (int iLine = 1; iLine <= m_parLogLine.size(); ++iLine) {
        ParLogLineType const line = m_parLogLine(iLine);
        int const curStrucMode = currentStrucStackTop();
        bool const executing =
            curStrucMode == ssmNotInStruc || curStrucMode == ssmDoThen || curStrucMode == ssmDoElse || curStrucMode == ssmDoCase;
        switch (line.statementKind) {
        case ksNone:
        case ksParameter:
        case ksRemark:
            // skip these lines
            break;
        case ksAssignment:
            if (executing) {
                evaluateExpression(line.startIntCode, line.endIntCode, resultString, resultReal, resultIsReal);
                int const curAssignParam = line.assignParam;
                if (curAssignParam >= 1) { // the Fortran wrote out of bounds when the parameter was not found
                    SymbolType &sym = m_symbol(curAssignParam);
                    sym.val = resultString;
                    // FORTRAN-COMPAT: isRealNum is not cleared when the result is a string
                    if (resultIsReal) {
                        sym.valAsReal = resultReal;
                        sym.isRealNum = true;
                    }
                }
            }
            break;
        case ksDisable:
        case ksEnable:
            if (executing) {
                int const curObjLine = line.objLine;
                if (curObjLine >= 1 && curObjLine <= m_objectLines.size()) {
                    m_objectLines(curObjLine).enabled = (line.statementKind == ksEnable);
                } else if (line.statementKind == ksDisable) {
                    addToErrMsg("Programming Error - invalid reference to ObjectLine (Disable).", msgWarning, 0);
                } else {
                    addToErrMsg("Programming Error - invalid reference to ObjectLine (Enable).", msgWarning, 0);
                }
            }
            break;
            // structure related
        case ksIf:
            switch (curStrucMode) {
            case ssmNotInStruc:
            case ssmDoThen:
            case ssmDoElse:
            case ssmDoCase:
                evaluateExpression(line.startIntCode, line.endIntCode, resultString, resultReal, resultIsReal);
                pushStrucStack(isResultTrue(resultString) ? ssmDoThen : ssmSkipToElse);
                break;
            case ssmSkipToElse:
            case ssmSkipToEndIf:
            case ssmSkipToEndSelect:
            case ssmFindCase: pushStrucStack(ssmSkipToEndIf); break;
            default: break;
            }
            break;
        case ksElseIf:
            switch (curStrucMode) {
            case ssmDoThen: // jump to end of 'if' block
                replaceTopStrucStack(ssmSkipToEndIf);
                break;
            case ssmSkipToElse:
                evaluateExpression(line.startIntCode, line.endIntCode, resultString, resultReal, resultIsReal);
                replaceTopStrucStack(isResultTrue(resultString) ? ssmDoThen : ssmSkipToElse);
                break;
            case ssmNotInStruc:
            case ssmFindCase:
            case ssmDoCase:
            case ssmDoElse: addToErrMsg("Not expecting ElseIf statement.", msgWarning, 0); break;
            default: break;
            }
            break;
        case ksElse:
            switch (curStrucMode) {
            case ssmDoThen: // jump to end of 'if' block
                replaceTopStrucStack(ssmSkipToEndIf);
                break;
            case ssmSkipToElse: replaceTopStrucStack(ssmDoElse); break;
            case ssmNotInStruc:
            case ssmFindCase:
            case ssmDoCase:
            case ssmDoElse: addToErrMsg("Not expecting Else statement.", msgWarning, 0); break;
            default: break;
            }
            break;
        case ksEndIf:
            switch (curStrucMode) {
            case ssmDoThen:
            case ssmDoElse:
            case ssmSkipToEndIf:
            case ssmSkipToElse: popStrucStack(); break;
            case ssmNotInStruc:
            case ssmFindCase:
            case ssmDoCase:
            case ssmSkipToEndSelect: addToErrMsg("Not expecting EndIf statement.", msgWarning, 0); break;
            default: break;
            }
            break;
        case ksSelect:
            switch (curStrucMode) {
            case ssmNotInStruc:
            case ssmDoThen:
            case ssmDoElse:
            case ssmDoCase:
                evaluateExpression(line.startIntCode, line.endIntCode, resultString, resultReal, resultIsReal);
                pushStrucStack(ssmFindCase, &resultString);
                break;
            case ssmFindCase: addToErrMsg("Expecting case statement.", msgWarning, 0); break;
            case ssmSkipToElse:
            case ssmSkipToEndIf:
            case ssmSkipToEndSelect: pushStrucStack(ssmSkipToEndSelect); break;
            default: break;
            }
            break;
        case ksCase:
            switch (curStrucMode) {
            case ssmFindCase: {
                int const curSym = line.symbolA;
                if (curSym >= 1 && curSym <= m_symbol.size()) {
                    if (doesTopMatch(m_symbol(curSym).val)) replaceTopStrucStack(ssmDoCase);
                }
                break;
            }
            case ssmDoCase: replaceTopStrucStack(ssmSkipToEndSelect); break;
            case ssmNotInStruc:
            case ssmDoThen:
            case ssmSkipToElse:
            case ssmDoElse: addToErrMsg("Not expecting Case statement.", msgWarning, 0); break;
            default: break;
            }
            break;
        case ksDefault:
            switch (curStrucMode) {
            case ssmFindCase: replaceTopStrucStack(ssmDoCase); break;
            case ssmDoCase: replaceTopStrucStack(ssmSkipToEndSelect); break;
            case ssmNotInStruc:
            case ssmDoThen:
            case ssmSkipToElse:
            case ssmDoElse: addToErrMsg("Not expecting Default statement.", msgWarning, 0); break;
            default: break;
            }
            break;
        case ksEndSelect:
            switch (curStrucMode) {
            case ssmFindCase:
            case ssmDoCase:
            case ssmSkipToEndSelect: popStrucStack(); break;
            case ssmNotInStruc:
            case ssmDoThen:
            case ssmDoElse:
            case ssmSkipToEndIf:
            case ssmSkipToElse: addToErrMsg("Not expecting EndSelect statement.", msgWarning, 0); break;
            default: break;
            }
            break;
        default: break;
        }
    }
}

//----------------------------------------------------------------------------------
// Execute the intermediate code for each embedding expression.
//----------------------------------------------------------------------------------
void Preprocessor::computeEmbeddedExpressions()
{
    if (m_verboseDebug) std::cout << " Started ComputeEmbeddedExpressions\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    double resultReal = 0.0;
    bool resultIsReal = false;
    for (int iFndExp = 1; iFndExp <= m_foundExpression.size(); ++iFndExp) {
        FoundExpressionType const expr = m_foundExpression(iFndExp);
        std::string result;
        evaluateExpression(expr.startIntCode, expr.endIntCode, result, resultReal, resultIsReal);
        m_foundExpression(iFndExp).expResult = result;
    }
}

//----------------------------------------------------------------------------------
// Open the intermediate file for reading and one of the output files for writing
//----------------------------------------------------------------------------------
void Preprocessor::openFileSecondPass(int caseIn)
{
    if (m_verboseDebug) std::cout << " Started OpenFileSecondPass\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    std::string const tempFileName = intermediateFileName();
    m_outputFileName = rtrim(m_outputFileNameRoot) + "-" + rtrim(m_cases(caseIn).suffix) + ".idf";
    if (fileExists(tempFileName)) {
        m_tempFileIn.open(tempFileName);
        if (!m_tempFileIn.is_open()) addToErrMsg("error opening to read intermediate file named parametric.int", msgError, 0);
        m_outFile.open(m_outputFileName, std::ios::out | std::ios::trunc);
        if (!m_outFile.is_open()) addToErrMsg("error opening output file" + m_outputFileName, msgError, 0);
        if (m_verboseDebug) std::cout << "   Output file:" << m_outputFileName << '\n';
    } else {
        addToErrMsg("error opening intermediate file - no file found", msgError, 0);
    }
}

//----------------------------------------------------------------------------------
// Copy the intermediate file to the output file but substitute values for
// embedded expressions and remove disabled objects
//----------------------------------------------------------------------------------
void Preprocessor::substituteValues()
{
    if (m_verboseDebug) std::cout << " Started SubstituteValues\n";
    if (m_errorCondition) return; // if an error exists do not run this routine
    std::string lineIn;
    std::string lineOut;
    int lineCount = 0;
    int curFoundExpression = 0;
    int curObjectLines = 0;
    int nextLineWithExpression = 0;
    int nextFirstLineDisabledObj = 0;
    int nextLastLineDisabledObj = 0;
    // get the initial special line
    getNextFoundExpression(reset, curFoundExpression, nextLineWithExpression);
    getNextDisabledObjectLines(reset, curObjectLines, nextFirstLineDisabledObj, nextLastLineDisabledObj);
    bool writeLine = true;
    while (readLine(m_tempFileIn, lineIn)) {
        replaceTabs(lineIn);
        ++lineCount; // count of lines in intermediate file
        if (m_verboseDebug && lineCount % 500 == 1) std::cout << "    " << formatI6(lineCount) << "  " << rtrim(lineIn) << '\n';
        if (lineCount == nextLineWithExpression) {
            lineOut = lineIn;
            for (;;) {
                int const found = indexOf(lineOut, "=@@" + intToStr6(curFoundExpression));
                if (found >= 1) {
                    // substitute the result for the expression
                    lineOut = sub(lineOut, 1, found - 1) + rtrim(m_foundExpression(curFoundExpression).expResult) + subFrom(lineOut, found + 9);
                } else {
                    lineOut = lineIn; // use input line without substitution
                    addToErrMsg("Programming Error - Did not find correct expression reference." + rtrim(lineIn), msgWarning, curFoundExpression);
                }
                getNextFoundExpression(lineCount, curFoundExpression, nextLineWithExpression);
                if (nextLineWithExpression != lineCount) break;
            }
        } else {
            lineOut = lineIn; // for most lines just echo the input line
        }
        if (lineCount == nextFirstLineDisabledObj) {
            writeLine = false;
        } else if (lineCount == nextLastLineDisabledObj + 1) {
            writeLine = true;
            getNextDisabledObjectLines(lineCount, curObjectLines, nextFirstLineDisabledObj, nextLastLineDisabledObj);
        }
        // if object is disabled, then don't write the line
        if (writeLine) m_outFile << rtrim(lineOut) << '\n';
    }
    if (m_verboseDebug) {
        std::cout << " In SubstituteValues, done copying the intermediate file. Line: " << formatI6(lineCount) << '\n';
    }
}

//----------------------------------------------------------------------------------
// Close the output file and the intermediate file
//----------------------------------------------------------------------------------
void Preprocessor::closeFileSecondPass()
{
    if (m_verboseDebug) std::cout << " Started CloseFileSecondPass\n";
    if (m_outFile.is_open()) m_outFile.close();
    if (m_tempFileIn.is_open()) m_tempFileIn.close();
    m_tempFileIn.clear();
}

//----------------------------------------------------------------------------------
// Write to the error file and close it. If no errors are reported, delete the error file.
//----------------------------------------------------------------------------------
void Preprocessor::writeErrorsAndCloseFile()
{
    if (!m_errFile.is_open()) return;
    auto writeKind = [this](int kind, char const *title) {
        bool wroteHeader = false;
        for (ErrMsgType const &msg : m_errMsgs) {
            if (msg.msgKind != kind) continue;
            if (!wroteHeader) {
                wroteHeader = true;
                m_errFile << "===================\n" << title << "\n===================\n";
            }
            m_errFile << "-------------------------------------------------------------------------\n";
            m_errFile << "      " << rtrim(msg.msgText) << '\n';
            m_errFile << "      Number " << formatI4(msg.msgErrNum) << '\n';
            m_errFile << "      Context " << rtrim(msg.msgContext) << '\n';
        }
    };
    // write errors first then the warnings
    writeKind(msgError, "   ERRORS          ");
    writeKind(msgWarning, "   WARNINGS        ");
    m_errFile.close();
    if (m_errMsgs.empty()) std::remove(m_errorFileName.c_str()); // get rid of error file if no errors found
}

//=====================================================================================
//
//    NON-GENERIC SUPPORT ROUTINES
//
//=====================================================================================

//----------------------------------------------------------------------------------
// Get the next disabled object and return the index to the ObjectLines array
// as well as the first and last line numbers of the object. The returned index is the first
// object after the lineCountIn line number that is disabled. If no other objects exist return
// zeros. A special lineCountIn value of "reset" sets the count to one.
//----------------------------------------------------------------------------------
void Preprocessor::getNextDisabledObjectLines(int lineCountIn, int &foundObjectLinesOut, int &firstLineOut, int &lastLineOut)
{
    int const numObjectLines = m_objectLines.size();
    if (numObjectLines >= 1) {
        if (lineCountIn == reset) m_indexObjectLines = 1;
        for (int i = 1; i <= 5000; ++i) { // run away condition prevention
            // FORTRAN-COMPAT: the index is incremented before the first test so the first
            // object in the file is never removed
            ++m_indexObjectLines;
            if (m_indexObjectLines <= numObjectLines) {
                ObjectLinesType const &obj = m_objectLines(m_indexObjectLines);
                if (!obj.enabled && obj.firstLine > lineCountIn) {
                    foundObjectLinesOut = m_indexObjectLines;
                    firstLineOut = obj.firstLine;
                    lastLineOut = obj.lastLine;
                    break;
                }
            } else {
                foundObjectLinesOut = 0;
                firstLineOut = 0;
                lastLineOut = 0;
                break;
            }
        }
    } else {
        foundObjectLinesOut = 0;
        firstLineOut = 0;
        lastLineOut = 0;
    }
}

//----------------------------------------------------------------------------------
// Get the next found expression and return the index to the foundExprssion array
// as well as the line number of the next expression. The returned index is the first
// expression after the lineCountIn line number. If no other expressions exist return
// zeros. A special lineCountIn value of "reset" sets the count to one.
//----------------------------------------------------------------------------------
void Preprocessor::getNextFoundExpression(int lineCountIn, int &foundExpressionOut, int &lineNumberOut)
{
    int const numFoundExpression = m_foundExpression.size();
    if (numFoundExpression >= 1) {
        if (lineCountIn == reset) {
            m_indexFoundExpression = 1;
            foundExpressionOut = m_indexFoundExpression;
            lineNumberOut = m_foundExpression(m_indexFoundExpression).line;
        } else {
            for (int i = 1; i <= 1000; ++i) { // run away condition prevention
                ++m_indexFoundExpression;
                if (m_indexFoundExpression <= numFoundExpression) {
                    if (m_foundExpression(m_indexFoundExpression).line >= lineCountIn) {
                        foundExpressionOut = m_indexFoundExpression;
                        lineNumberOut = m_foundExpression(m_indexFoundExpression).line;
                        break;
                    }
                } else {
                    foundExpressionOut = 0;
                    lineNumberOut = 0;
                    break;
                }
            }
        }
    } else {
        foundExpressionOut = 0;
        lineNumberOut = 0;
    }
}

//----------------------------------------------------------------------------------
// Evaluate an expression already in intermediate code and return result
//----------------------------------------------------------------------------------
void Preprocessor::evaluateExpression(
    int firstIntCodeIn, int lastIntCodeIn, std::string &resultStringOut, double &resultRealOut, bool &resultIsRealOut)
{
    int const numIntCode = m_intCode.size();
    if (!(firstIntCodeIn >= 1 && firstIntCodeIn <= numIntCode && lastIntCodeIn >= 1 && lastIntCodeIn <= numIntCode)) {
        resultStringOut.clear();
        resultRealOut = 0.0;
        resultIsRealOut = false;
        addToErrMsg("Programming Error - token not recognized while evaluating expression.", msgWarning, 0);
        return;
    }

    // Compare the top two values as numbers if both are numeric, otherwise as strings.
    auto compare = [this](auto realCompare, auto stringCompare) {
        if (isEvalStackTopReal(2)) { // check if top two values are both real
            double const realX = popEvalReal();
            double const realY = popEvalReal();
            pushEvalLogical(realCompare(realY, realX));
        } else {
            std::string const stringX = popEvalString();
            std::string const stringY = popEvalString();
            pushEvalLogical(stringCompare(fortranCompare(stringY, stringX)));
        }
    };
    auto binaryReal = [this](auto op) {
        double const realX = popEvalReal();
        double const realY = popEvalReal();
        pushEvalReal(op(realY, realX));
    };
    auto unaryReal = [this](auto op) {
        double const realX = popEvalReal();
        pushEvalReal(op(realX));
    };

    for (int iIntCode = firstIntCodeIn; iIntCode <= lastIntCodeIn; ++iIntCode) {
        int const code = m_intCode(iIntCode);
        if (code >= 1) { // all positive values are references to the symbol table
            pushSymbolOnEvalStack(code);
            continue;
        }
        switch (code) {
        case tokNum:
        case tokStr:
        case tokID:
        case tokRtParen:
        case tokFunc:
        case tokLtParen:
        case tokTilde:
        case tokNone:
        case tokINVALID: addToErrMsg("Programming Error - invalid token found while evaluating expression.", msgWarning, 0); break;
        case tokPlus: binaryReal([](double y, double x) { return y + x; }); break;
        case tokMinus: binaryReal([](double y, double x) { return y - x; }); break;
        case tokTimes: binaryReal([](double y, double x) { return y * x; }); break;
        case tokDiv: binaryReal([](double y, double x) { return y / x; }); break;
        case tokExp: binaryReal([](double y, double x) { return std::pow(y, x); }); break;
        case tokGT: compare([](double y, double x) { return y > x; }, [](int c) { return c > 0; }); break;
        case tokEQ: compare([](double y, double x) { return y == x; }, [](int c) { return c == 0; }); break;
        case tokLT: compare([](double y, double x) { return y < x; }, [](int c) { return c < 0; }); break;
        case tokGE: compare([](double y, double x) { return y >= x; }, [](int c) { return c >= 0; }); break;
        case tokLE: compare([](double y, double x) { return y <= x; }, [](int c) { return c <= 0; }); break;
        case tokNE:
            // FORTRAN-COMPAT: the original evaluated <> the same as >
            compare([](double y, double x) { return y > x; }, [](int c) { return c > 0; });
            break;
        case tokAnd: {
            bool const booleanX = popEvalLogical();
            bool const booleanY = popEvalLogical();
            pushEvalLogical(booleanY && booleanX);
            break;
        }
        case tokOr: {
            bool const booleanX = popEvalLogical();
            bool const booleanY = popEvalLogical();
            pushEvalLogical(booleanY || booleanX);
            break;
        }
        case tokUnNeg: unaryReal([](double x) { return -x; }); break;
        case tokFuncABS: unaryReal([](double x) { return std::fabs(x); }); break;
        case tokFuncACOS: unaryReal([](double x) { return std::acos(x); }); break;
        case tokFuncASIN: unaryReal([](double x) { return std::asin(x); }); break;
        case tokFuncATAN: unaryReal([](double x) { return std::atan(x); }); break;
        case tokFuncCOS: unaryReal([](double x) { return std::cos(x); }); break;
        case tokFuncEXP: unaryReal([](double x) { return std::exp(x); }); break;
        case tokFuncINT:
            // FORTRAN-COMPAT: REAL(INT(x),8) converts through a default (32 bit) integer,
            // which gives -2147483648 on x86 for NaN and values out of range, and +0 (never
            // -0) for values between -1 and 0
            unaryReal([](double x) {
                if (std::isnan(x) || x >= 2147483648.0 || x <= -2147483649.0) return -2147483648.0;
                return static_cast<double>(static_cast<std::int32_t>(x));
            });
            break;
        case tokFuncLEN: {
            std::string const stringX = popEvalString();
            pushEvalReal(static_cast<double>(lenTrim(stringX)));
            break;
        }
        case tokFuncLOG: unaryReal([](double x) { return std::log(x); }); break;
        case tokFuncMOD: binaryReal([](double y, double x) { return std::fmod(y, x); }); break;
        case tokFuncNOT: {
            bool const booleanX = popEvalLogical();
            pushEvalLogical(!booleanX);
            break;
        }
        case tokFuncSIN: unaryReal([](double x) { return std::sin(x); }); break;
        case tokFuncSQRT: unaryReal([](double x) { return std::sqrt(x); }); break;
        case tokFuncTAN: unaryReal([](double x) { return std::tan(x); }); break;
        default: addToErrMsg("Programming Error - token not recognized while evaluating expression.", msgWarning, 0); break;
        }
    }
    if (m_topEvalStack >= 1) {
        EvalStackType const &top = m_evalStack(m_topEvalStack);
        resultIsRealOut = top.isRealNum;
        resultRealOut = top.valAsReal;
        resultStringOut = resultIsRealOut ? adjustl(top.val) : top.val;
        --m_topEvalStack;
    } else {
        resultStringOut.clear();
        resultRealOut = 0.0;
        resultIsRealOut = false;
        addToErrMsg("Programming Error - evaluation stack empty.", msgWarning, 0);
    }
}

//----------------------------------------------------------------------------------
// Add one top location in the evaluation stack
// Used by the PushEval subroutines.
//----------------------------------------------------------------------------------
void Preprocessor::addToEvalStack()
{
    ++m_topEvalStack;
    if (m_topEvalStack > m_evalStack.size()) m_evalStack.resize(m_topEvalStack);
}

//----------------------------------------------------------------------------------
// Add the symbol value (parameter or constant) onto the evaluation stack
//----------------------------------------------------------------------------------
void Preprocessor::pushSymbolOnEvalStack(int symbolRefIn)
{
    if (symbolRefIn >= 1 && symbolRefIn <= m_symbol.size()) {
        addToEvalStack();
        SymbolType const &sym = m_symbol(symbolRefIn);
        EvalStackType &top = m_evalStack(m_topEvalStack);
        top.val = sym.val;
        top.isRealNum = sym.isRealNum;
        top.valAsReal = sym.valAsReal;
    } else {
        addToErrMsg("Programming Error - invalid symbol reference.", msgWarning, 0);
    }
}

//----------------------------------------------------------------------------------
// Take the top value from the evaluation stack as a real (numeric) value
//----------------------------------------------------------------------------------
double Preprocessor::popEvalReal()
{
    if (m_topEvalStack < 1) return 0.0;
    double result = 0.0;
    EvalStackType const &top = m_evalStack(m_topEvalStack);
    if (top.isRealNum) {
        result = top.valAsReal;
    } else {
        addToErrMsg("Real value expected but not found on evaluation stack.", msgWarning, 0);
    }
    --m_topEvalStack;
    return result;
}

//----------------------------------------------------------------------------------
// Add a real (numeric) value onto the evaluation stack
//----------------------------------------------------------------------------------
void Preprocessor::pushEvalReal(double realIn)
{
    addToEvalStack();
    EvalStackType &top = m_evalStack(m_topEvalStack);
    top.val = realToString(realIn);
    top.isRealNum = true;
    top.valAsReal = realIn;
}

//----------------------------------------------------------------------------------
// Take the top value from the evaluation stack as a logical value (True or False)
//----------------------------------------------------------------------------------
bool Preprocessor::popEvalLogical()
{
    if (m_topEvalStack < 1) {
        addToErrMsg("Logical value expected but not found on evaluation stack.", msgWarning, 0);
        return false;
    }
    EvalStackType const &top = m_evalStack(m_topEvalStack);
    bool const result = top.isRealNum ? (top.valAsReal != 0.0) : isStrEq(sub(top.val, 1, 1), "T");
    --m_topEvalStack;
    return result;
}

//----------------------------------------------------------------------------------
// Add logical value (True or False) onto the evaluation stack
//----------------------------------------------------------------------------------
void Preprocessor::pushEvalLogical(bool logicalIn)
{
    addToEvalStack();
    EvalStackType &top = m_evalStack(m_topEvalStack);
    top.val = logicalIn ? "True" : "False";
    top.isRealNum = false;
    top.valAsReal = logicalIn ? 1.0 : 0.0;
    if (m_verboseDebug) std::cout << (logicalIn ? "pushEvalLogical - TRUE\n" : "pushEvalLogical - FALSE\n");
}

//----------------------------------------------------------------------------------
// Take the top value from the evaluation stack as a character string value
//----------------------------------------------------------------------------------
std::string Preprocessor::popEvalString()
{
    if (m_topEvalStack < 1) return std::string();
    std::string result = m_evalStack(m_topEvalStack).val;
    --m_topEvalStack;
    return result;
}

//----------------------------------------------------------------------------------
// Test of the top values of the stack are real numeric values
//----------------------------------------------------------------------------------
bool Preprocessor::isEvalStackTopReal(int numValsIn)
{
    if (numValsIn >= 1 && numValsIn <= m_topEvalStack) {
        // FORTRAN-COMPAT: the original only examined the topmost entry on each pass of
        // its loop, so only the top value determines the result
        return m_evalStack(m_topEvalStack).isRealNum;
    }
    addToErrMsg("Programming Error - checking top of eval stack for reals failed.", msgWarning, 0);
    return false;
}

//----------------------------------------------------------------------------------
// Test if the result from an evaulated expression is True or False
//----------------------------------------------------------------------------------
bool Preprocessor::isResultTrue(std::string const &resultIn)
{
    // any text starting with T is considered true, everything else is false
    return isStrEq(sub(resultIn, 1, 1), "T");
}

//----------------------------------------------------------------------------------
// Push the current structure mode to the stack
//----------------------------------------------------------------------------------
void Preprocessor::pushStrucStack(int modeIn, std::string const *matchIn)
{
    ++m_topStrucStack;
    if (m_topStrucStack > m_strucStack.size()) m_strucStack.resize(m_topStrucStack);
    StrucStackType &top = m_strucStack(m_topStrucStack);
    top.mode = modeIn;
    if (matchIn != nullptr) top.match = *matchIn;
}

//----------------------------------------------------------------------------------
// Pop the top mode of the structure stack
//----------------------------------------------------------------------------------
int Preprocessor::popStrucStack()
{
    if (m_topStrucStack < 1) return 0;
    int const mode = m_strucStack(m_topStrucStack).mode;
    --m_topStrucStack;
    return mode;
}

//----------------------------------------------------------------------------------
// Return the mode of the top value of the structure stack
//----------------------------------------------------------------------------------
int Preprocessor::currentStrucStackTop() const
{
    if (m_topStrucStack < 1) return 0;
    return m_strucStack(m_topStrucStack).mode;
}

//----------------------------------------------------------------------------------
// Replace the mode of the top value of the structure stack
//----------------------------------------------------------------------------------
void Preprocessor::replaceTopStrucStack(int modeIn)
{
    if (m_topStrucStack >= 1) m_strucStack(m_topStrucStack).mode = modeIn;
}

//----------------------------------------------------------------------------------
// See if the stack Match string is the same as argument. This is used
// primarily for SELECT structure CASE statements
//----------------------------------------------------------------------------------
bool Preprocessor::doesTopMatch(std::string const &argIn) const
{
    if (m_topStrucStack < 1) return false;
    return isStrEq(argIn, m_strucStack(m_topStrucStack).match);
}

//----------------------------------------------------------------------------------
// Get the reference to the ObjectLines array that contains the references to all of
// the objects in the file based on one or two symbol references provided. This
// is primarily used for ENABLE and DISABLE commands.
//----------------------------------------------------------------------------------
int Preprocessor::getObjectReference(int nameOfObjectSymbolRefIn, int kindOfObjectSymbolRefIn)
{
    std::string nameOfObject;
    std::string kindOfObject;
    int found = 0;
    int countOfFound = 0;
    // use strings for search
    if (nameOfObjectSymbolRefIn >= 1) nameOfObject = m_symbol(nameOfObjectSymbolRefIn).val;
    if (kindOfObjectSymbolRefIn >= 1) kindOfObject = m_symbol(kindOfObjectSymbolRefIn).val;
    for (int iObjectLines = 1; iObjectLines <= m_objectLines.size(); ++iObjectLines) {
        ObjectLinesType const &obj = m_objectLines(iObjectLines);
        if (kindOfObjectSymbolRefIn == 0) {
            if (isStrEq(nameOfObject, obj.nameOfObj)) {
                found = iObjectLines;
                ++countOfFound;
            }
        } else { // if kind of object is provided then search for exact match
            if (m_verboseDebug) std::cout << "getObjectReference: " << rtrim(obj.nameOfObj) << "::" << rtrim(obj.kindOfObj) << '\n';
            if (isStrEq(nameOfObject, obj.nameOfObj) && isStrEq(kindOfObject, obj.kindOfObj)) {
                found = iObjectLines;
                ++countOfFound;
            }
        }
    }
    if (countOfFound == 1) return found; // this is the expected result
    if (countOfFound == 0) {
        addToErrMsg("Reference to object name not found: " + rtrim(nameOfObject), msgWarning, 0);
    } else {
        addToErrMsg("Reference to object name not unique: " + rtrim(nameOfObject), msgWarning, 0);
    }
    return 0;
}

//----------------------------------------------------------------------------------
// Uses the expressionToRPN to identify one or two constants and returns the
// references to them in the symbol array.
//----------------------------------------------------------------------------------
void Preprocessor::getConstants(std::string const &stringIn, int &aSymbolOut, int *bSymbolOut)
{
    int startIntCode = 0;
    int endIntCode = 0;
    expressionToRPN(stringIn, startIntCode, endIntCode);
    if (!(startIntCode >= 1 && endIntCode >= startIntCode)) return;
    int const numSymbol = m_symbol.size();
    // see if one or two symbols are needed
    if (bSymbolOut != nullptr) {
        if (endIntCode == startIntCode) {
            aSymbolOut = m_intCode(startIntCode);
            *bSymbolOut = 0;
        } else if (endIntCode == startIntCode + 1) {
            aSymbolOut = m_intCode(startIntCode);
            *bSymbolOut = m_intCode(endIntCode);
        }
        if (aSymbolOut < 1 || aSymbolOut > numSymbol) aSymbolOut = 0;
        if (*bSymbolOut < 1 || *bSymbolOut > numSymbol) *bSymbolOut = 0;
    } else { // just one constant needed
        if (endIntCode == startIntCode) {
            aSymbolOut = m_intCode(startIntCode);
            // check if just a symbol
            if (aSymbolOut < 1 || aSymbolOut > numSymbol) aSymbolOut = 0;
        }
    }
}

//----------------------------------------------------------------------------------
// Convert an expression into a reverse polish notation representation
// Use Edsger W. Dijkstra's Shunting-yard algorithm
// from input string
//----------------------------------------------------------------------------------
void Preprocessor::expressionToRPN(std::string const &expressionIn, int &firstIntCodeOut, int &lastIntCodeOut)
{
    m_errorContext = rtrim(expressionIn);
    int prevClassToken = tokNone;
    int prevTokenEnd = 0;
    int oldOp = 0;
    int oldPrec = precNoItem;
    int curFuncStart = 0;
    int curFuncEnd = 0;
    if (m_verboseDebug) std::cout << "expressionToRPN expressionIn: " << rtrim(expressionIn) << '\n';
    firstIntCodeOut = m_intCode.size() + 1;
    for (;;) {
        int newTokenStart = 0;
        int newTokenEnd = 0;
        int classToken = tokNone;
        getNextToken(expressionIn, prevTokenEnd, prevClassToken, newTokenStart, newTokenEnd, classToken);
        if (newTokenEnd == 0) break;
        std::string const curTokenString = sub(expressionIn, newTokenStart, newTokenEnd);
        std::string const opString = opToken2String(classToken);
        if (m_verboseDebug) {
            std::cout << "Token: {" << rtrim(curTokenString) << "} {" << opString << "}   " << formatI4(classToken) << '\n';
        }
        int const curPrec = classifyPrecedence(classToken);
        switch (classToken) {
        case tokNum:
            addConstantSymbol(curTokenString, stringToReal(curTokenString));
            addIntCode(m_symbol.size()); // add the newest symbol to the IntCode list
            break;
        case tokStr:
            addConstantSymbol(curTokenString);
            addIntCode(m_symbol.size()); // add the newest symbol to the IntCode list
            break;
        case tokID: {
            int const curID = lookupParameterSymbol(curTokenString);
            if (curID >= 1) {
                addIntCode(curID);
            } else {
                addToErrMsg("Invalid parameter name: " + rtrim(curTokenString), msgError, 0);
            }
            break;
        }
        // FORTRAN-COMPAT: tokFuncACOS is missing from this list in the original
        case tokFuncABS:
        case tokFuncASIN:
        case tokFuncATAN:
        case tokFuncCOS:
        case tokFuncEXP:
        case tokFuncINT:
        case tokFuncLEN:
        case tokFuncLOG:
        case tokFuncMOD:
        case tokFuncNOT:
        case tokFuncSIN:
        case tokFuncSQRT:
        case tokFuncTAN: pushOpStack(classToken, precLeftParen, newTokenStart, newTokenEnd); break;
        case tokOr:
        case tokAnd:
        case tokGT:
        case tokEQ:
        case tokLT:
        case tokGE:
        case tokLE:
        case tokNE:
        case tokTimes:
        case tokDiv:
        case tokPlus:
        case tokMinus:
        case tokExp:
            for (;;) {
                oldPrec = checkTopPrec();
                if (curPrec <= oldPrec) {
                    popOpStack(oldOp, oldPrec, curFuncStart, curFuncEnd);
                    addIntCode(oldOp);
                } else {
                    break;
                }
            }
            pushOpStack(classToken, curPrec, newTokenStart, newTokenEnd);
            break;
        case tokLtParen: pushOpStack(classToken, precLeftParen, newTokenStart, newTokenEnd); break;
        case tokRtParen:
            for (;;) {
                if (m_opStackTop == 0) {
                    // The Fortran read before the start of the operator stack at this point;
                    // stop processing the parenthesis instead.
                    addToErrMsg("Stack underflow. ", msgError, 0);
                    break;
                }
                popOpStack(oldOp, oldPrec, curFuncStart, curFuncEnd);
                if (oldPrec == precLeftParen) break;
                addIntCode(oldOp);
            }
            if (checkTopIsFunc()) {
                popOpStack(oldOp, oldPrec, curFuncStart, curFuncEnd);
                addIntCode(oldOp);
            }
            break;
        default:
            // tokTilde is not used since multiple value functions are not supported
            break;
        }
        prevTokenEnd = newTokenEnd;
        prevClassToken = classToken;
    }
    // pop the rest of the operator stack
    int const numOps = m_opStackTop;
    for (int i = 1; i <= numOps; ++i) {
        popOpStack(oldOp, oldPrec, curFuncStart, curFuncEnd);
        addIntCode(oldOp);
    }
    lastIntCodeOut = m_intCode.size();
    m_errorContext.clear();
}

//----------------------------------------------------------------------------------
// The routine returns the start and end positions in the string
// for the next token which can be an operator or a number or a
// function name.  The search starts each time at the position
// endPrevToken + 1 so that it can be called multiple times. If tokenEnd
// returns a zero, no more tokens were found. Normally endPrevToken
// is set to endNewToken from the previous call. Positions are 1-based.
//----------------------------------------------------------------------------------
void Preprocessor::getNextToken(
    std::string const &inString, int endPrevToken, int prevTokenClass, int &startNewToken, int &endNewToken, int &newTokenClass)
{
    constexpr int stNone = 1;
    constexpr int stNum = 2;
    constexpr int stStr = 3;
    constexpr int stID = 4;
    constexpr int stEqual = 5;
    constexpr int stGreat = 6;
    constexpr int stLess = 7;
    constexpr int stFunc = 8;
    constexpr int stMinus = 9;
    constexpr int stAmpersand = 10;
    constexpr int stPipe = 11;
    constexpr int stDONE = 100;

    startNewToken = 0;
    endNewToken = 0;
    newTokenClass = 0;
    int const lenIn = lenTrim(inString);
    if (endPrevToken >= lenIn) return;
    int prevCharKind = charNone;
    bool onePeriodInNumber = false;
    bool oneEInNumber = false;
    std::string errDescription;
    int state = stNone;
    // skip an extra character for the trailing double quote after a string
    int const startScan = (prevTokenClass != tokStr) ? endPrevToken + 1 : endPrevToken + 2;

    // after these tokens a minus sign is a negative number or unary negation
    auto minusIsNegation = [prevTokenClass]() {
        switch (prevTokenClass) {
        case tokNone:
        case tokMinus:
        case tokPlus:
        case tokTimes:
        case tokDiv:
        case tokExp:
        case tokLtParen: return true;
        default: return false;
        }
    };

    for (int i = startScan; i <= lenIn; ++i) {
        int const curCharKind = classifyChar(charAt(inString, i));
        switch (state) {
        case stNone:
            switch (curCharKind) {
            case charNum:
                state = stNum;
                startNewToken = i;
                endNewToken = i;
                newTokenClass = tokNum;
                break;
            case charPeriod:
                onePeriodInNumber = true;
                state = stNum;
                startNewToken = i;
                endNewToken = i;
                break;
            case charE:
            case charAZ:
            case charUnderscore:
                state = stFunc;
                startNewToken = i;
                endNewToken = i;
                break;
            case charMinus:
                state = stMinus;
                startNewToken = i;
                break;
            case charPlus:
            case charMult:
            case charDiv:
            case charExp:
            case charLeftParen:
            case charRightParen:
            case charTilde:
                startNewToken = i;
                endNewToken = i;
                state = stDONE;
                switch (curCharKind) {
                case charPlus: newTokenClass = tokPlus; break;
                case charMult: newTokenClass = tokTimes; break;
                case charDiv: newTokenClass = tokDiv; break;
                case charExp: newTokenClass = tokExp; break;
                case charLeftParen: newTokenClass = tokLtParen; break;
                case charRightParen: newTokenClass = tokRtParen; break;
                default: newTokenClass = tokTilde; break;
                }
                break;
            case charDoubleQuote:
                state = stStr;
                startNewToken = i;
                break;
            case charEqual:
                state = stEqual;
                startNewToken = i;
                break;
            case charGreat:
                state = stGreat;
                startNewToken = i;
                break;
            case charLess:
                state = stLess;
                startNewToken = i;
                break;
            case charSpace: state = stNone; break;
            case charDollar:
                state = stID;
                startNewToken = i;
                break;
            case charAmpersand:
                state = stAmpersand;
                startNewToken = i;
                break;
            case charPipe:
                state = stPipe;
                startNewToken = i;
                break;
            case charOther:
                newTokenClass = tokINVALID;
                errDescription = "Invalid character";
                state = stDONE;
                break;
            default: break;
            }
            break;
        case stNum:
            switch (curCharKind) {
            case charNum:
                endNewToken = i;
                newTokenClass = tokNum;
                break;
            case charPeriod:
                if (onePeriodInNumber) { // already have a period in this number
                    newTokenClass = tokINVALID;
                    errDescription = "Only one period allowed in a number";
                    state = stDONE;
                } else {
                    endNewToken = i;
                    onePeriodInNumber = true;
                }
                break;
            case charE:
                if (oneEInNumber) { // already have an E in this number
                    newTokenClass = tokINVALID;
                    errDescription = "Only one E or D allowed in a number";
                    state = stDONE;
                } else {
                    endNewToken = i;
                    oneEInNumber = true;
                }
                break;
            case charMinus: // accept a minus if just after the E for negative exponents.
                if (prevCharKind == charE) {
                    endNewToken = i;
                } else {
                    newTokenClass = tokNum;
                    state = stDONE;
                }
                break;
            default:
                newTokenClass = tokNum;
                state = stDONE;
                break;
            }
            break;
        case stStr:
            if (curCharKind == charDoubleQuote) {
                endNewToken = i;
                newTokenClass = tokStr;
                state = stDONE;
            }
            break;
        case stID: // a dollar sign followed by letters and numbers
            switch (curCharKind) {
            case charNum:
            case charE:
            case charAZ:
            case charUnderscore:
                endNewToken = i;
                newTokenClass = tokID;
                break;
            default:
                newTokenClass = tokID;
                state = stDONE;
                break;
            }
            break;
        case stEqual:
            switch (curCharKind) {
            case charEqual:
                endNewToken = i;
                newTokenClass = tokEQ;
                state = stDONE;
                break;
            case charGreat:
                endNewToken = i;
                newTokenClass = tokGE;
                state = stDONE;
                break;
            case charLess:
                endNewToken = i;
                newTokenClass = tokLE;
                state = stDONE;
                break;
            default:
                newTokenClass = tokINVALID;
                errDescription = "An equals sign alone is invalid for expressions.";
                state = stDONE;
                break;
            }
            break;
        case stGreat:
            if (curCharKind == charEqual) {
                endNewToken = i;
                newTokenClass = tokGE;
            } else {
                endNewToken = startNewToken;
                newTokenClass = tokGT;
            }
            state = stDONE;
            break;
        case stLess:
            if (curCharKind == charGreat) {
                endNewToken = i;
                newTokenClass = tokNE;
            } else if (curCharKind == charEqual) {
                endNewToken = i;
                newTokenClass = tokLE;
            } else {
                endNewToken = startNewToken;
                newTokenClass = tokLT;
            }
            state = stDONE;
            break;
        case stFunc: // functions are alphas only
            switch (curCharKind) {
            case charE:
            case charAZ:
            case charUnderscore: endNewToken = i; break;
            default:
                newTokenClass = tokFunc;
                state = stDONE;
                break;
            }
            break;
        case stMinus: // can be an operator, unary minus, or the beginning of a number
            // Cases with minus sign
            //    8-FUNC(2) operand
            //    8-$VAR operand
            //    8-4 operand
            //    $VAR-2 operand
            //
            //    8*-2 negative
            //    -2 negative
            //    FUNC(-2) negative
            //    FUNC(3~-2~8) negative (remember ~ is parameter separator instead of comma
            //
            //    8*-$VAR unary negation operand
            //    -$VAR  unary negation operand
            if (minusIsNegation()) {
                switch (curCharKind) {
                case charNum:
                    endNewToken = i;
                    state = stNum;
                    break;
                case charPeriod:
                    endNewToken = i;
                    onePeriodInNumber = true;
                    state = stNum;
                    break;
                default:
                    // FORTRAN-COMPAT: the unary negation token also consumes the following
                    // character
                    endNewToken = i;
                    newTokenClass = tokUnNeg;
                    state = stDONE;
                    break;
                }
            } else {
                endNewToken = startNewToken;
                newTokenClass = tokMinus;
                state = stDONE;
            }
            break;
        case stAmpersand:
            if (curCharKind == charAmpersand) {
                endNewToken = i;
                newTokenClass = tokAnd;
            } else {
                newTokenClass = tokINVALID;
                errDescription = "A single ampersand & alone is invalid for expressions.";
            }
            state = stDONE;
            break;
        case stPipe:
            if (curCharKind == charPipe) {
                endNewToken = i;
                newTokenClass = tokOr;
            } else {
                newTokenClass = tokINVALID;
                errDescription = "A single pipe | alone is invalid for expressions.";
            }
            state = stDONE;
            break;
        default: break;
        }
        prevCharKind = curCharKind;
        if (state == stDONE) break;
    }
    // look up the specific function
    if (newTokenClass == tokFunc) {
        std::string const curFuncString = sub(inString, startNewToken, endNewToken);
        if (isStrEq(curFuncString, "ABS")) {
            newTokenClass = tokFuncABS;
        } else if (isStrEq(curFuncString, "ACOS")) { // arc cos
            newTokenClass = tokFuncACOS;
        } else if (isStrEq(curFuncString, "ASIN")) { // arc sin
            newTokenClass = tokFuncASIN;
        } else if (isStrEq(curFuncString, "ATAN")) { // arc tan
            newTokenClass = tokFuncATAN;
        } else if (isStrEq(curFuncString, "COS")) {
            newTokenClass = tokFuncCOS;
        } else if (isStrEq(curFuncString, "EXP")) { // e to the x
            newTokenClass = tokFuncEXP;
        } else if (isStrEq(curFuncString, "INT")) {
            newTokenClass = tokFuncINT;
        } else if (isStrEq(curFuncString, "LEN")) { // length of string
            newTokenClass = tokFuncINT;             // FORTRAN-COMPAT: original maps LEN to INT
        } else if (isStrEq(curFuncString, "LOG")) {
            newTokenClass = tokFuncLOG;
        } else if (isStrEq(curFuncString, "MOD")) { // remainder of argument
            newTokenClass = tokFuncLOG;             // FORTRAN-COMPAT: original maps MOD to LOG
        } else if (isStrEq(curFuncString, "NOT")) {
            newTokenClass = tokFuncNOT;
        } else if (isStrEq(curFuncString, "SIN")) {
            newTokenClass = tokFuncSIN;
        } else if (isStrEq(curFuncString, "SQRT")) {
            newTokenClass = tokFuncSQRT;
        } else if (isStrEq(curFuncString, "TAN")) {
            newTokenClass = tokFuncTAN;
        } else {
            errDescription = "Function referenced not found.";
            newTokenClass = tokINVALID;
        }
    }
    // for strings take off the first and last character which will be double quotes
    if (newTokenClass == tokStr) {
        --endNewToken;
        ++startNewToken;
    }
    // check for invalid token
    if (newTokenClass == tokINVALID) {
        addToErrMsg("Invalid token found: " + errDescription + " " + rtrim(inString), msgWarning, startNewToken);
        std::cout << "CONTEXT: " << rtrim(inString) << '\n';
        std::cout << std::string(static_cast<std::size_t>(std::max(startNewToken + 8, 0)), ' ') << "^\n";
        std::cout << errDescription << '\n';
    }
}

//----------------------------------------------------------------------------------
// Classify the kind of statement in the given parametric:logic field and
// return a constant for the kind of statement (see constants at top of file)
// and return the character after the command
//----------------------------------------------------------------------------------
void Preprocessor::determineStatement(std::string const &inString, int &outKind, int &outAfter)
{
    struct Keyword
    {
        char const *text;
        int kind;
    };
    // the order matters since only the leading characters are compared
    static Keyword const keywords[] = {{"PARAMETER", ksParameter},
                                       {"IF", ksIf},
                                       {"ELSEIF", ksElseIf},
                                       {"ELSE", ksElse},
                                       {"ENDIF", ksEndIf},
                                       {"SELECT", ksSelect},
                                       {"CASE", ksCase},
                                       {"DEFAULT", ksDefault},
                                       {"ENDSELECT", ksEndSelect},
                                       {"DISABLE", ksDisable},
                                       {"ENABLE", ksEnable},
                                       {"REMARK", ksRemark}};
    std::string const curInput = adjustl(inString);
    for (Keyword const &keyword : keywords) {
        int const len = static_cast<int>(std::char_traits<char>::length(keyword.text));
        if (isStrEq(sub(curInput, 1, len), keyword.text)) {
            outKind = keyword.kind;
            outAfter = len + 1;
            return;
        }
    }
    if (charAt(curInput, 1) == '$') {
        // confirm that it is an assignment by verifying that an equals sign follows the
        // identifier - still need to do more to verify that the identifier is valid.
        int const equalsPos = indexOf(curInput, "=");
        if (equalsPos >= 3) {
            outKind = ksAssignment;
            outAfter = equalsPos;
            return;
        }
    }
    outKind = ksNone;
    outAfter = 1;
}

//----------------------------------------------------------------------------------
// Add one IntCode - intermediate code
//----------------------------------------------------------------------------------
void Preprocessor::addIntCode(int intCodeIn)
{
    m_intCode.push_back(intCodeIn);
}

//----------------------------------------------------------------------------------
// Add to the list of line references for all objects in the input file
//----------------------------------------------------------------------------------
void Preprocessor::addObjectLineReference(std::string const &objKind, std::string const &objName, int lineStart, int lineEnd)
{
    ObjectLinesType obj;
    obj.kindOfObj = objKind;
    obj.nameOfObj = objName;
    obj.firstLine = lineStart;
    obj.lastLine = lineEnd;
    obj.enabled = true;
    m_objectLines.push_back(obj);
    if (m_verboseDebug) {
        std::cout << "AddObjectLineReference: " << formatI6(m_objectLines.size()) << "  " << rtrim(objKind) << "  " << rtrim(objName)
                  << "  " << formatI6(lineStart) << "  " << formatI6(lineEnd) << '\n';
    }
}

//----------------------------------------------------------------------------------
// Find the current parameter index from a given name
//----------------------------------------------------------------------------------
int Preprocessor::lookupParameterSymbol(std::string const &symbolNameIn) const
{
    for (int iSymbol = 1; iSymbol <= m_lastSymbolParameter; ++iSymbol) {
        if (isStrEq(symbolNameIn, m_symbol(iSymbol).name)) return iSymbol;
    }
    return 0; // zero means not found
}

//----------------------------------------------------------------------------------
// Add a new object to the list of objects - holds only the PARAMETRIC: objects
//----------------------------------------------------------------------------------
void Preprocessor::addNewObject(int kindOfObject)
{
    IdfObjectType obj;
    obj.kindObj = kindOfObject;
    // assume that the next field is the first and last field of the object until
    // additional fields are added.
    obj.firstField = m_idfField.size() + 1;
    obj.lastField = m_idfField.size() + 1;
    m_idfObject.push_back(obj);
}

//----------------------------------------------------------------------------------
// Add a new field to the current object - holds only the fields from PARAMETRIC: objects
//----------------------------------------------------------------------------------
void Preprocessor::addField(std::string const &fieldIn)
{
    m_idfField.push_back(adjustl(fieldIn));
    m_idfObject(m_idfObject.size()).lastField = m_idfField.size();
}

//----------------------------------------------------------------------------------
// Add a new parameter to the list of symbols.
// Return a false if an error occurred
//----------------------------------------------------------------------------------
bool Preprocessor::addParameterSymbol(std::string const &parIn, bool ifSetValueForRunIn)
{
    std::string const curPar = adjustl(parIn);
    // confirm that it leads with a $
    if (charAt(curPar, 1) != '$') return false;
    // confirm that the parameter name is alphanumeric
    for (int iChar = 2; iChar <= lenTrim(curPar); ++iChar) {
        switch (classifyChar(charAt(curPar, iChar))) {
        case charNum:
        case charAZ:
        case charE:
        case charUnderscore: break;
        default: return false;
        }
    }
    SymbolType sym;
    sym.name = curPar;
    sym.isParameter = true;
    sym.isSetValueForRun = ifSetValueForRunIn;
    m_symbol.push_back(sym);
    if (ifSetValueForRunIn) m_lastSymbolSetValueForRun = m_symbol.size();
    if (m_verboseDebug) std::cout << "  Started AddParameterSymbol: " << curPar << '\n';
    m_lastSymbolParameter = m_symbol.size();
    return true;
}

//----------------------------------------------------------------------------------
// Add a new constant to the list of symbols. If value is a number the numerical
// real value is provided. No verification that the string and number
// are equivalent is done here. Must be verified by calling routine.
//----------------------------------------------------------------------------------
void Preprocessor::addConstantSymbol(std::string const &constantIn)
{
    SymbolType sym;
    sym.val = constantIn; // name is blank for constants
    m_symbol.push_back(sym);
}

void Preprocessor::addConstantSymbol(std::string const &constantIn, double constantRealIn)
{
    SymbolType sym;
    sym.val = constantIn; // name is blank for constants
    sym.valAsReal = constantRealIn;
    sym.isRealNum = true;
    m_symbol.push_back(sym);
}

//----------------------------------------------------------------------------------
// Add the expressions found in the IDF file, not including in the parametric
// objects, to the list.
//----------------------------------------------------------------------------------
int Preprocessor::addNewExpression(std::string const &expString, int lineOfFile)
{
    if (m_verboseDebug) std::cout << rtrim(expString) << "  " << formatI6(lineOfFile) << '\n';
    FoundExpressionType expr;
    expr.text = expString;
    expr.line = lineOfFile;
    m_foundExpression.push_back(expr);
    return m_foundExpression.size(); // return the counter
}

//----------------------------------------------------------------------------------
// Store the error messages and set the errorCondition flag so that messages
// are included in file at end.
//----------------------------------------------------------------------------------
void Preprocessor::addToErrMsg(std::string const &textOfError, int kindOfError, int errorNumber)
{
    ErrMsgType msg;
    // the stored messages were limited to the length of the Fortran character variables
    msg.msgText = rtrim(textOfError).substr(0, maxErrorLength);
    msg.msgKind = kindOfError;
    msg.msgErrNum = errorNumber;
    msg.msgContext = m_errorContext.substr(0, maxErrorLength);
    m_errMsgs.push_back(msg);
    std::cout << "-------------------------------------------------------------------------\n";
    std::cout << (kindOfError == msgError ? "   ERROR:\n" : "   WARNING:\n");
    std::cout << "      " << rtrim(textOfError) << '\n';
    std::cout << "      Number " << formatI4(errorNumber) << '\n';
    if (lenTrim(m_errorContext) > 0) std::cout << "   CONTEXT:" << rtrim(m_errorContext) << '\n';
    m_errorContext.clear();
    if (kindOfError == msgError) m_errorCondition = true;
}

//----------------------------------------------------------------------------------
// Push an operator on to the operation stack for converting expressions into RPN
//----------------------------------------------------------------------------------
void Preprocessor::pushOpStack(int opTok, int prec, int startFunc, int endFunc)
{
    ++m_opStackTop;
    if (m_opStackTop > m_opStack.size()) m_opStack.resize(m_opStackTop);
    OpStackType &top = m_opStack(m_opStackTop);
    top.tokOperator = opTok;
    top.precedence = prec;
    top.funcStart = startFunc;
    top.funcEnd = endFunc;
}

//----------------------------------------------------------------------------------
// Pop an operator off the top of the operation stack for converting expressions into RPN
//----------------------------------------------------------------------------------
void Preprocessor::popOpStack(int &opTok, int &prec, int &startFunc, int &endFunc)
{
    if (m_opStackTop < 1) {
        addToErrMsg("Operator stack underflow.", msgWarning, 0);
        opTok = 0;
        prec = precNoItem;
        startFunc = 0;
        endFunc = 0;
        return;
    }
    OpStackType const &top = m_opStack(m_opStackTop);
    opTok = top.tokOperator;
    prec = top.precedence;
    startFunc = top.funcStart;
    endFunc = top.funcEnd;
    --m_opStackTop;
}

//----------------------------------------------------------------------------------
// Check the top item in the operation stack for it precedence. This is used
// for converting expressions into RPN
//----------------------------------------------------------------------------------
int Preprocessor::checkTopPrec() const
{
    if (m_opStackTop >= 1) return m_opStack(m_opStackTop).precedence;
    return precNoItem;
}

//----------------------------------------------------------------------------------
// Check the top item in the operation stack if it is a function. This is used
// for converting expressions into RPN
//----------------------------------------------------------------------------------
bool Preprocessor::checkTopIsFunc() const
{
    if (m_opStackTop < 1) return false;
    int const op = m_opStack(m_opStackTop).tokOperator;
    return op <= tokFuncABS && op >= tokFuncTAN;
}

//----------------------------------------------------------------------------------
// Convert the operation token back into a string
//----------------------------------------------------------------------------------
std::string Preprocessor::opToken2String(int tokenIn)
{
    switch (tokenIn) {
    // tokens with normal output
    case tokPlus: return "+ ";
    case tokMinus: return "- ";
    case tokTimes: return "* ";
    case tokDiv: return "/ ";
    case tokExp: return "^ ";
    case tokRtParen: return ") ";
    case tokLtParen: return "(";
    case tokGT: return "> ";
    case tokEQ: return "==";
    case tokLT: return "< ";
    case tokGE: return ">=";
    case tokLE: return "<=";
    case tokNE: return "<>";
    case tokTilde: return "~ ";
    case tokAnd: return "&&";
    case tokOr: return "||";
    case tokFuncABS: return "ABS";
    case tokFuncACOS: return "ACOS";
    case tokFuncASIN: return "ASIN";
    case tokFuncATAN: return "ATAN";
    case tokFuncCOS: return "COS";
    case tokFuncEXP: return "EXP";
    case tokFuncINT: return "INT";
    case tokFuncLEN: return "LEN";
    case tokFuncLOG: return "LOG";
    case tokFuncMOD: return "MOD";
    case tokFuncNOT: return "NOT";
    case tokFuncSIN: return "SIN";
    case tokFuncSQRT: return "SQRT";
    case tokFuncTAN: return "TAN";
    // tokens with output describing the string
    case tokNone: return "None";
    case tokNum: return "Num";
    case tokStr: return "Str";
    case tokID: return "ID";
    case tokUnNeg: return "UnNg";
    case tokINVALID: return "INVL";
    default:
        // FORTRAN-COMPAT: reached for an incomplete token such as a lone "." at the end
        // of an expression
        addToErrMsg("Unknown type of operator", msgWarning, tokenIn);
        return "????";
    }
}

//----------------------------------------------------------------------------------
// Determine the precedence of the token provided for converting expressions
// into a reverse polish notation represetation.
//----------------------------------------------------------------------------------
int Preprocessor::classifyPrecedence(int tokenIn)
{
    switch (tokenIn) {
    case tokExp: return precExp;
    case tokTimes:
    case tokDiv: return precMultDiv;
    case tokPlus:
    case tokMinus: return precAddSub;
    case tokGT:
    case tokEQ:
    case tokLT:
    case tokGE:
    case tokLE:
    case tokNE: return precCompare;
    case tokAnd:
    case tokOr: return precAndOr;
    case tokRtParen: return precLeftParen;
    default: return precNoItem;
    }
}

} // namespace ParametricPreprocessor
