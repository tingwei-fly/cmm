#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "header.h"
#include "symbolTable.h"
/* This file is for reference only, you are not required to follow the implementation. 
 * You only need to check for errors stated in the hw4 assignment document. */

#define FATAL(x) { \
    fprintf(stderr, "Fatal : " x "\nAborted.\n"); \
    exit(1); \
}

#define DECL(node) (node)->semantic_value.declSemanticValue
#define STMT(node) (node)->semantic_value.stmtSemanticValue
#define EXPR(node) (node)->semantic_value.exprSemanticValue
#define ID(node) (node)->semantic_value.identifierSemanticValue

#define FOR_SIBLINGS(name, node) \
    for(AST_NODE* (name) = (node) \
        ; (name) \
        ; (name) = (name)->rightSibling)

int g_anyErrorOccur = 0;
int g_suppressError = 0;
AST_NODE *g_currentFunctionDecl = NULL;

DATA_TYPE getBiggerType(DATA_TYPE dataType1, DATA_TYPE dataType2);
void processProgramNode(AST_NODE *programNode);
void processDeclarationNode(AST_NODE* declarationNode);
void declareIdList(AST_NODE* typeNode, SymbolAttributeKind isVariableOrTypeAttribute, int ignoreArrayFirstDimSize);
void declareFunction(AST_NODE* returnTypeNode);
int processDeclDimList(AST_NODE* variableDeclDimList, TypeDescriptor* desc, int ignoreFirstDimSize);
DATA_TYPE processTypeNode(AST_NODE* typeNode);
void processBlockNode(AST_NODE* blockNode);
void processStmtNode(AST_NODE* stmtNode);
void checkAssignOrExpr(AST_NODE* assignOrExprRelatedNode);
void checkWhileStmt(AST_NODE* whileNode);
void checkForStmt(AST_NODE* forNode);
void checkIfStmt(AST_NODE* ifNode);
void checkWriteFunction(AST_NODE* functionCallNode);
DATA_TYPE checkFunctionCall(AST_NODE* functionCallNode);
DATA_TYPE processExprRelatedNode(AST_NODE* exprRelatedNode);
void checkParameterPassing(Parameter* formalParameter, AST_NODE* actualParameter);
void checkReturnStmt(AST_NODE* returnNode);
DATA_TYPE processExprNode(AST_NODE* exprNode);
DATA_TYPE processVariableLValue(AST_NODE* idNode);
DATA_TYPE processConstValueNode(AST_NODE* constValueNode);
DATA_TYPE getExprOrConstValue(AST_NODE* exprOrConstNode, int* iValue, float* fValue);
void evaluateExprValue(AST_NODE* exprNode, DATA_TYPE datatype);

void processVariableDeclListNode(AST_NODE* decl_list);

void dumpNode(AST_NODE *node);
void dumpExprNode(AST_NODE *node);
void dumpStmtNode(AST_NODE *node);
void dumpConstNode(AST_NODE *node);
void dumpIdNode(AST_NODE *node);


extern SymbolAttribute readAttr;
extern SymbolAttribute freadAttr;
extern SymbolAttribute writeAttr;

static __inline__ char* getIdByNode(AST_NODE *node) {
    return node->semantic_value.identifierSemanticValue.identifierName;
}

typedef enum ErrorMsgKind {
    SYMBOL_IS_NOT_TYPE,
    SYMBOL_REDECLARE,
    SYMBOL_UNDECLARED,
    NOT_FUNCTION_NAME,
    TRY_TO_INIT_ARRAY,
    EXCESSIVE_ARRAY_DIM_DECLARATION,
    RETURN_ARRAY,
    VOID_VARIABLE,
    TYPEDEF_VOID_ARRAY,
    PARAMETER_TYPE_UNMATCH,
    TOO_FEW_ARGUMENTS,
    TOO_MANY_ARGUMENTS,
    RETURN_TYPE_UNMATCH,
    INCOMPATIBLE_ARRAY_DIMENSION,
    NOT_ASSIGNABLE,
    NOT_ARRAY,
    IS_TYPE_NOT_VARIABLE,
    IS_FUNCTION_NOT_VARIABLE,
    STRING_OPERATION,
    VOID_OPERATION,
    ARRAY_SIZE_NOT_INT,
    ARRAY_SIZE_NEGATIVE,
    ARRAY_SUBSCRIPT_NOT_INT,
    PASS_ARRAY_TO_SCALAR,
    PASS_SCALAR_TO_ARRAY,
    IMPLICIT_CONVERSION
} ErrorMsgKind;


void printErrorMsgSpecial(AST_NODE* node1, char* name2, ErrorMsgKind errorMsgKind) {
    if(g_suppressError) return;
    g_anyErrorOccur = 1;
    printf("Error found in line %d\n", node1->linenumber);

    switch(errorMsgKind)
    {
        case PASS_ARRAY_TO_SCALAR:
            printf("Array %s passed to scalar parameter %s.\n", getIdByNode(node1), name2);
            break;
        case PASS_SCALAR_TO_ARRAY:
            if(node1->nodeType == IDENTIFIER_NODE)
                printf("Scalar %s passed to array parameter %s.\n", getIdByNode(node1), name2);
            else
                printf("Scalar expression passed to array parameter %s.\n", name2);
            break;
        default:
            FATAL("Unexcepted execution flow!");
            break;
    }
}

void printErrorMsg(AST_NODE* node, ErrorMsgKind errorMsgKind) {
    if(g_suppressError) return;
    g_anyErrorOccur = 1;
    printf("Error found in line %d\n", node->linenumber);
    switch(errorMsgKind)
    {
        case SYMBOL_REDECLARE:
            printf("ID %s redeclared.\n", getIdByNode(node));
            break;
        case SYMBOL_UNDECLARED:
            printf("ID %s undeclared.\n", getIdByNode(node));
            break;
        case TOO_FEW_ARGUMENTS:
            printf("too few arguments to function %s.\n", getIdByNode(node));
            break;
        case TOO_MANY_ARGUMENTS:
            printf("too many arguments to function %s.\n", getIdByNode(node));
            break;
        case RETURN_TYPE_UNMATCH:
            printf("Incompatible return type.\n");
            break;
        case INCOMPATIBLE_ARRAY_DIMENSION:
            printf("Incompatible array dimensions.\n");
            break;
        case ARRAY_SIZE_NOT_INT:
            printf("Array size is not an integer.\n");
            break;
        case ARRAY_SIZE_NEGATIVE:
            printf("Array size is negative.\n");
            break;
        case ARRAY_SUBSCRIPT_NOT_INT:
            printf("Array subscript is not an integer.\n");
            break;
        /* following errors are not specified in the spec */
        case NOT_FUNCTION_NAME:
            printf("ID %s is not a function.\n", getIdByNode(node));
            break;
        case SYMBOL_IS_NOT_TYPE:
            printf("%s doesn't named a type.\n", getIdByNode(node));
            break;
        case VOID_VARIABLE:
            printf("ID %s is of type void.\n", getIdByNode(node));
            break;
        case NOT_ARRAY:
            printf("ID %s is not an array.\n", getIdByNode(node));
            break;
        case IS_FUNCTION_NOT_VARIABLE:
            printf("ID %s is a function, not variable.\n", getIdByNode(node));
            break;
        case IS_TYPE_NOT_VARIABLE:
            printf("ID %s is a type, not variable.\n", getIdByNode(node));
            break;
        case EXCESSIVE_ARRAY_DIM_DECLARATION:
            printf("ID %s exceeds maximum array dimension.\n", getIdByNode(node));
            break;
        case STRING_OPERATION:
            printf("Try to operate on a string.\n");
            break;
        case VOID_OPERATION:
            printf("Try to operate on a function without return value.\n");
            break;
        case PARAMETER_TYPE_UNMATCH:
            printf("Parameter type unmatch.\n");
            break;
        case NOT_ASSIGNABLE:
        case RETURN_ARRAY:
        case TYPEDEF_VOID_ARRAY:
        case TRY_TO_INIT_ARRAY:
        default:
            FATAL("Invalid ErrorMsgKind!");
            break;
        }
}

void printWarnMsg(AST_NODE* node, ErrorMsgKind errorMsgKind) {
    printf("Warning in line %d\n", node->linenumber);
    switch(errorMsgKind)
    {
        case IMPLICIT_CONVERSION:
            printf("Implicit return type conversion.\n");
            break;
        default:
            FATAL("Unexcepted execution flow!");
            break;
    }
}

SymbolAttribute* newAttribute(SymbolAttributeKind attributeKind) {
    SymbolAttribute *temp = (SymbolAttribute*)malloc(sizeof(struct SymbolAttribute));
    temp->attributeKind = attributeKind;
    /* need to specify attr later  */
    return temp;
}

TypeDescriptor* newTypeDesc(TypeDescriptorKind kind) {
    TypeDescriptor *temp = (TypeDescriptor*)malloc(sizeof(struct TypeDescriptor));
    temp->kind = kind;
    /* need to sepcify properties later */
    return temp;
}

SymbolTableEntry* insertType(char *name, DATA_TYPE type) {
    SymbolAttribute *attr = newAttribute(TYPE_ATTRIBUTE);
    TypeDescriptor *desc = newTypeDesc(SCALAR_TYPE_DESCRIPTOR);
    desc->properties.dataType = type;
    attr->attr.typeDescriptor = desc;
    return enterSymbol(name, attr);
}

void semanticAnalysis(AST_NODE *root) {
    /* Preinsert types */
    insertType(SYMBOL_TABLE_INT_NAME, INT_TYPE);
    insertType(SYMBOL_TABLE_FLOAT_NAME, FLOAT_TYPE);
    insertType(SYMBOL_TABLE_VOID_NAME, VOID_TYPE);
    /* preinsert read, fread, write? */
    enterSymbol(SYMBOL_TABLE_SYS_LIB_READ, &readAttr);
    enterSymbol(SYMBOL_TABLE_SYS_LIB_FREAD, &freadAttr);
    enterSymbol(SYMBOL_TABLE_SYS_LIB_WRITE, &writeAttr);

    processProgramNode(root);
}


DATA_TYPE getBiggerType(DATA_TYPE dataType1, DATA_TYPE dataType2) {
    if(dataType1 == FLOAT_TYPE || dataType2 == FLOAT_TYPE) {
        return FLOAT_TYPE;
    } else {
        return INT_TYPE;
    }
}

void processProgramNode(AST_NODE *programNode) {
    FOR_SIBLINGS(global_decl_list, programNode->child) {
        /* process global_decl */
        switch(global_decl_list->nodeType){
            /* two case: decl_list, function_decl */
            case VARIABLE_DECL_LIST_NODE:
                processVariableDeclListNode(global_decl_list);
                break;
            case DECLARATION_NODE:
                processDeclarationNode(global_decl_list);
                break;
            default:
                printf("Invalid program\n");
                exit(1);
        }
    }
}

void processVariableDeclListNode(AST_NODE *decl_list_node) {
    AST_NODE *decl_list = decl_list_node->child;
    while(decl_list){
        /* process decl */
        processDeclarationNode(decl_list);
        decl_list = decl_list->rightSibling;
    }
}

void processDeclarationNode(AST_NODE* declarationNode) {
    assert(declarationNode->nodeType == DECLARATION_NODE);
    switch(DECL(declarationNode).kind){
        case VARIABLE_DECL:
            /* var_decl */
            declareIdList(declarationNode, VARIABLE_ATTRIBUTE, 0);
            break;
        case TYPE_DECL:
            /* type_decl */
            declareIdList(declarationNode, TYPE_ATTRIBUTE, 0);
            break;
        case FUNCTION_DECL:
            declareFunction(declarationNode);
            break;
        case FUNCTION_PARAMETER_DECL:
            /* impossible, we can not see function parameter decl before function decl. */
            FATAL("Broken AST\n");
        default:
            printf("Invalid declaration type\n");
    }
}


DATA_TYPE processTypeNode(AST_NODE* idNodeAsType) {
    char *type = getIdByNode(idNodeAsType);
    SymbolTableEntry* entry = retrieveSymbol(type);
    if(entry){
        if(entry->attribute->attributeKind != TYPE_ATTRIBUTE){
            return ERROR_TYPE;
        }
        else{
            /* this is unnecessary? */
            idNodeAsType->semantic_value.identifierSemanticValue.symbolTableEntry = entry;
            return entry->attribute->attr.typeDescriptor->properties.dataType;
        }
    }
    else{
        return ERROR_TYPE;
    }
}

void declareIdList(AST_NODE* declarationNode,
                   SymbolAttributeKind isVariableOrTypeAttribute,
                   __attribute__((unused))int ignoreArrayFirstDimSize) {
    if(isVariableOrTypeAttribute == VARIABLE_ATTRIBUTE){
        AST_NODE* type_node = declarationNode->child;
        DATA_TYPE datatype = processTypeNode(type_node);
        if(datatype == ERROR_TYPE){
            printErrorMsg(type_node, SYMBOL_IS_NOT_TYPE);
            return;
        } else if(datatype == VOID_TYPE) {
            printErrorMsg(type_node->rightSibling, VOID_VARIABLE);
        }

        FOR_SIBLINGS(id_list, type_node->rightSibling){
            AST_NODE *expr_node;
            DATA_TYPE init_type;
            TypeDescriptor *typedesc;
            SymbolAttribute *symbolattr = newAttribute(VARIABLE_ATTRIBUTE);
            switch(id_list->semantic_value.identifierSemanticValue.kind){
                case WITH_INIT_ID:
                    expr_node = id_list->child;
                    init_type = processExprRelatedNode(expr_node);
                    if(init_type == CONST_STRING_TYPE) {
                        printErrorMsg(expr_node, STRING_OPERATION);
                        break;
                    }
                    if(init_type == VOID_TYPE) {
                        printErrorMsg(expr_node, VOID_OPERATION);
                        break;
                    }
                    if(init_type == ERROR_TYPE){
                        break;
                    }
                    /*
                    if(datatype == INT_TYPE && init_type == FLOAT_TYPE){
                        // error: can not assign float to int ?
                        break;
                    }*/
                    typedesc = newTypeDesc(SCALAR_TYPE_DESCRIPTOR);
                    typedesc->properties.dataType = datatype;
                    symbolattr->attr.typeDescriptor = typedesc;
                    if(enterSymbol(getIdByNode(id_list), symbolattr) == NULL){
                        printErrorMsg(id_list, SYMBOL_REDECLARE);
                        break;
                    }
                    break;
                case NORMAL_ID:
                    typedesc = newTypeDesc(SCALAR_TYPE_DESCRIPTOR);
                    typedesc->properties.dataType = datatype;
                    symbolattr->attr.typeDescriptor = typedesc;
                    if(enterSymbol(getIdByNode(id_list), symbolattr) == NULL){
                        printErrorMsg(id_list, SYMBOL_REDECLARE);
                    }
                    break;
                case ARRAY_ID:
                    /* child: dim_decl */
                    typedesc = newTypeDesc(ARRAY_TYPE_DESCRIPTOR);
                    typedesc->properties.arrayProperties.elementType = datatype;
                    processDeclDimList(id_list->child, typedesc, 0);

                    symbolattr->attr.typeDescriptor = typedesc;
                    if(enterSymbol(getIdByNode(id_list), symbolattr) == NULL){
                        printErrorMsg(id_list, SYMBOL_REDECLARE);
                    }
                    break;
                default:
                    printf("unexpected kind of variable\n");
                    break;
            }
        }
    }
    else if(isVariableOrTypeAttribute == TYPE_ATTRIBUTE){
        AST_NODE* type_node = declarationNode->child;
        DATA_TYPE datatype = processTypeNode(type_node);
        if(datatype == ERROR_TYPE){
            printErrorMsg(type_node, SYMBOL_IS_NOT_TYPE);
            return;
        }
        FOR_SIBLINGS(id_list, type_node->rightSibling)
            if(insertType(getIdByNode(id_list), datatype) == NULL)
                printErrorMsg(id_list, SYMBOL_REDECLARE);
        return;
    }
}

void checkAssignOrExpr(AST_NODE* assignOrExprRelatedNode) {
    switch(assignOrExprRelatedNode->nodeType) {
        /* only identifier */
        case IDENTIFIER_NODE:
            processVariableLValue(assignOrExprRelatedNode);
            break;
        /* Assignment */
        case STMT_NODE:
            processStmtNode(assignOrExprRelatedNode);
            break;
        /* Relop Expr */
        case EXPR_NODE:
            processExprRelatedNode(assignOrExprRelatedNode);
            break;
        default:
            FATAL("Invalid node type in for loop");
            break;
    }
}

void checkWhileStmt(AST_NODE* whileNode) {
    processExprRelatedNode(whileNode->child);
    processStmtNode(whileNode->child->rightSibling);
}


void checkForStmt(AST_NODE* forNode) {
    AST_NODE *cur = forNode->child;
    if(cur->nodeType == NONEMPTY_ASSIGN_EXPR_LIST_NODE) {
        FOR_SIBLINGS(child, cur->child)
            checkAssignOrExpr(child);
    }
    cur = cur->rightSibling;
    if(cur->nodeType == NONEMPTY_RELOP_EXPR_LIST_NODE) {
        FOR_SIBLINGS(child, cur->child)
            checkAssignOrExpr(child);
    }
    cur = cur->rightSibling;
    if(cur->nodeType == NONEMPTY_ASSIGN_EXPR_LIST_NODE) {
        FOR_SIBLINGS(child, cur->child)
            checkAssignOrExpr(child);
    }
    cur = cur->rightSibling;
    processStmtNode(cur);
}

void checkIfStmt(AST_NODE* ifNode) {
    AST_NODE *cur;
    cur = ifNode->child;
    processExprRelatedNode(cur);
    cur = cur->rightSibling;
    processStmtNode(cur);
    cur = cur->rightSibling;
    if(cur->nodeType != NUL_NODE){
        processStmtNode(cur);
    }
}

void checkWriteFunction(AST_NODE* funcNameNode) {
    if(funcNameNode->rightSibling->nodeType == NUL_NODE) {
        printErrorMsg(funcNameNode, TOO_FEW_ARGUMENTS);
        return;
    }
    AST_NODE* arg = funcNameNode->rightSibling->child;
    if(arg->rightSibling != NULL) {
        printErrorMsg(funcNameNode, TOO_MANY_ARGUMENTS);
        return;
    }
    DATA_TYPE argType = processExprRelatedNode(arg);
    if(argType == VOID_TYPE) {
        printErrorMsg(funcNameNode, VOID_OPERATION);
    }
}

DATA_TYPE checkFunctionCall(AST_NODE* functionCallNode) {
    AST_NODE* funcNameNode = functionCallNode->child;
    SymbolTableEntry *entry = retrieveSymbol(getIdByNode(funcNameNode));
    /* ID exist ? */
    if(entry == NULL) {
        printErrorMsg(funcNameNode, SYMBOL_UNDECLARED);
        return ERROR_TYPE;
    }
    /* ID is of type function? */
    if(entry->attribute->attributeKind != FUNCTION_SIGNATURE) {
        printErrorMsg(funcNameNode, NOT_FUNCTION_NAME);
        return ERROR_TYPE;
    }
    /* check parameters */
    FunctionSignature *sign = entry->attribute->attr.functionSignature;
    if(!strncmp(getIdByNode(funcNameNode), "write", 6)) {
        checkWriteFunction(funcNameNode);
        return VOID_TYPE;
    }
    if(sign->parametersCount == 0) {
        if(funcNameNode->rightSibling->nodeType != NUL_NODE) {
            printErrorMsg(funcNameNode, TOO_MANY_ARGUMENTS);
            return ERROR_TYPE;
        }
    } else {
        checkParameterPassing(sign->parameterList, funcNameNode);
    }
    return sign->returnType;
}

void checkParameterPassing(Parameter* param, AST_NODE* funcNameNode) {
    AST_NODE *arg = funcNameNode->rightSibling->child;
    while(param) {
        if(!arg) {
            printErrorMsg(funcNameNode, TOO_FEW_ARGUMENTS);
            break;
        }
        switch(arg->nodeType) {
            case CONST_VALUE_NODE:
            case IDENTIFIER_NODE:
            case EXPR_NODE:
            case STMT_NODE:
                if(param->type->kind == SCALAR_TYPE_DESCRIPTOR) {
                    /* check if arg is scalar */
                    if(arg->nodeType == IDENTIFIER_NODE) {
                        SymbolTableEntry *entry = retrieveSymbol(getIdByNode(arg));
                        if(entry->attribute->attributeKind == VARIABLE_ATTRIBUTE) {
                            if(entry->attribute->attr.typeDescriptor->kind == ARRAY_TYPE_DESCRIPTOR) {
                                int argDim = entry->attribute->attr.typeDescriptor->properties.arrayProperties.dimension;
                                int refDim = 0;
                                if(arg->child != NULL)
                                    refDim = processDeclDimList(arg->child, NULL, 0);
                                if(argDim > refDim) {
                                    printErrorMsgSpecial(arg, param->parameterName, PASS_ARRAY_TO_SCALAR);
                                    g_suppressError = 1;
                                }
                                else if(argDim < refDim) {
                                    printErrorMsg(arg, INCOMPATIBLE_ARRAY_DIMENSION);
                                    g_suppressError = 1;
                                }
                            }
                        }
                    }
                    DATA_TYPE argType = processExprRelatedNode(arg);
                    if(argType == VOID_TYPE)
                        printErrorMsg(arg, VOID_OPERATION);
                    else if(argType == CONST_STRING_TYPE)
                        printErrorMsg(arg, PARAMETER_TYPE_UNMATCH);
                } else if(param->type->kind == ARRAY_TYPE_DESCRIPTOR) {
                    /* check if arg is ptr */
                    if(arg->nodeType == IDENTIFIER_NODE) {
                        SymbolTableEntry *entry = retrieveSymbol(getIdByNode(arg));
                        if(entry->attribute->attributeKind == VARIABLE_ATTRIBUTE) {
                            if(entry->attribute->attr.typeDescriptor->kind == ARRAY_TYPE_DESCRIPTOR) {
                                int argDim = entry->attribute->attr.typeDescriptor->properties.arrayProperties.dimension;
                                int paramDim = param->type->properties.arrayProperties.dimension;
                                int refDim = 0;
                                if(arg->child != NULL)
                                    refDim = processDeclDimList(arg->child, NULL, 0);
                                if(refDim == argDim)
                                    printErrorMsgSpecial(arg, param->parameterName, PASS_SCALAR_TO_ARRAY);
                                else if(refDim > argDim || argDim - refDim != paramDim)
                                    printErrorMsg(arg, INCOMPATIBLE_ARRAY_DIMENSION);
                                g_suppressError = 1;
                            }
                        }
                    }
                    DATA_TYPE argType = processExprRelatedNode(arg);
                    if(argType == VOID_TYPE)
                        printErrorMsg(arg, VOID_OPERATION);
                    else if(argType == CONST_STRING_TYPE)
                        printErrorMsg(arg, PARAMETER_TYPE_UNMATCH);
                    else if(argType != ERROR_TYPE)
                        printErrorMsgSpecial(arg, param->parameterName, PASS_SCALAR_TO_ARRAY);
                }
                arg = arg->rightSibling;
                break;
            default: FATAL("Broken AST");
        }
        g_suppressError = 0;
        param = param->next;
    }
    if(arg)
        printErrorMsg(funcNameNode, TOO_MANY_ARGUMENTS);
}

DATA_TYPE processExprRelatedNode(AST_NODE* exprRelatedNode) {
    /* exprnode, constnode, stmtnode(function call), idnode(variable reference), nullnode
     * Now I process assign_stmt together because it is more straightforward. */
    if(exprRelatedNode->nodeType == EXPR_NODE){
        return processExprNode(exprRelatedNode);
    }
    else if(exprRelatedNode->nodeType == CONST_VALUE_NODE){
        return processConstValueNode(exprRelatedNode);
    }
    else if(exprRelatedNode->nodeType == STMT_NODE){
        AST_NODE *lvalue_node;
        switch(STMT(exprRelatedNode).kind){
            case FUNCTION_CALL_STMT:
                return checkFunctionCall(exprRelatedNode);
            case ASSIGN_STMT: {
                lvalue_node = exprRelatedNode->child;
                DATA_TYPE ldatatype = processExprRelatedNode(lvalue_node);
                if(ldatatype == ERROR_TYPE || ldatatype == VOID_TYPE)
                    return ERROR_TYPE;
                DATA_TYPE rdatatype = processExprRelatedNode(lvalue_node->rightSibling);
                if(rdatatype != ldatatype) { 
                    if(rdatatype == CONST_STRING_TYPE) {
                        printErrorMsg(exprRelatedNode, STRING_OPERATION);
                        return ERROR_TYPE;
                    }
                    else if(rdatatype == VOID_TYPE) {
                        printErrorMsg(exprRelatedNode, VOID_OPERATION);
                        return ERROR_TYPE;
                    }
                    else if(rdatatype == ERROR_TYPE) {
                        return ERROR_TYPE;
                    }
                }
                /* what if rdatatype == ERROR_TYPE? */
                /* any error message? */
                return ldatatype;
            }
            default:
                FATAL("Broken AST");
        }
    }
    else if(exprRelatedNode->nodeType == IDENTIFIER_NODE){
        return processVariableLValue(exprRelatedNode);
    }
    else if(exprRelatedNode->nodeType == NUL_NODE){
        /* Maybe we don't need this? */
        return VOID_TYPE;
    }
    else{
        printf("Unexpected ExprRelated Node\n");
        return ERROR_TYPE;
    }
    return ERROR_TYPE;
}

DATA_TYPE getExprOrConstValue(AST_NODE* exprOrConstNode, int* iValue, float* fValue) {
    if(exprOrConstNode->nodeType == CONST_VALUE_NODE){
        switch(exprOrConstNode->semantic_value.const1->const_type){
            case INTEGERC:
                *iValue = exprOrConstNode->semantic_value.const1->const_u.intval;
                return INT_TYPE;
            case FLOATC:
                *fValue = exprOrConstNode->semantic_value.const1->const_u.fval;
                return FLOAT_TYPE;
            default:
                return ERROR_TYPE;
        }
    }
    else if(exprOrConstNode->nodeType == EXPR_NODE &&
            EXPR(exprOrConstNode).isConstEval == 1){
        switch(exprOrConstNode->dataType){
            case INT_TYPE:
                *iValue = exprOrConstNode->semantic_value.exprSemanticValue.constEvalValue.iValue;
                return INT_TYPE;
            case FLOAT_TYPE:
                *fValue = exprOrConstNode->semantic_value.exprSemanticValue.constEvalValue.fValue;
                return FLOAT_TYPE;
            default:
                return ERROR_TYPE;
        }
    }
    else{
        return ERROR_TYPE;
    }
}

void evaluateExprValue(AST_NODE* exprNode, DATA_TYPE datatype) {
    if(EXPR(exprNode).kind == BINARY_OPERATION){
        AST_NODE* lexpr = exprNode->child;
        AST_NODE* rexpr = lexpr->rightSibling;
        
        int l_int, r_int;
        float l_float, r_float;

        DATA_TYPE l_datatype = getExprOrConstValue(lexpr, &l_int, &l_float);
        DATA_TYPE r_datatype = getExprOrConstValue(rexpr, &r_int, &r_float);
        if(datatype != INT_TYPE && datatype != FLOAT_TYPE) {
            return;
        }
        if(l_datatype == ERROR_TYPE || r_datatype == ERROR_TYPE
            || l_datatype == VOID_TYPE || r_datatype == VOID_TYPE){
            /* error message? */
            return;
        }
        if(l_datatype == INT_TYPE && r_datatype == INT_TYPE){
            int val;
            switch(EXPR(exprNode).op.binaryOp){
                case BINARY_OP_ADD:
                    val = l_int + r_int;
                    break;
                case BINARY_OP_SUB:
                    val = l_int - r_int;
                    break;
                case BINARY_OP_MUL:
                    val = l_int * r_int;
                    break;
                case BINARY_OP_DIV:
                    val = l_int / r_int;
                    break;
                case BINARY_OP_EQ:
                    val = (l_int == r_int);
                    break;
                case BINARY_OP_GE:
                    val = (l_int >= r_int);
                    break;
                case BINARY_OP_LE:
                    val = (l_int <= r_int);
                    break;
                case BINARY_OP_NE:
                    val = (l_int != r_int);
                    break;
                case BINARY_OP_GT:
                    val = (l_int > r_int);
                    break;
                case BINARY_OP_LT:
                    val = (l_int < r_int);
                    break;
                case BINARY_OP_AND:
                    val = (l_int && r_int);
                    break;
                case BINARY_OP_OR:
                    val = (l_int || r_int);
                    break;
            }
            EXPR(exprNode).isConstEval = 1;
            if(datatype == INT_TYPE)
                exprNode->semantic_value.exprSemanticValue.constEvalValue.iValue = val;
            else if(datatype == FLOAT_TYPE)
                exprNode->semantic_value.exprSemanticValue.constEvalValue.fValue = (float)val;
        }
        else if(datatype == FLOAT_TYPE || r_datatype == FLOAT_TYPE){
            if(l_datatype == INT_TYPE)
                l_float = (float)l_int;
            if(r_datatype == INT_TYPE)
                r_float = (float)r_int;
            float val = .0;
            int ival = 0;
            switch(EXPR(exprNode).op.binaryOp) {
                case BINARY_OP_ADD:
                    val = l_float + r_float;
                    break;
                case BINARY_OP_SUB:
                    val = l_float - r_float;
                    break;
                case BINARY_OP_MUL:
                    val = l_float * r_float;
                    break;
                case BINARY_OP_DIV:
                    val = l_float / r_float;
                    break;
                case BINARY_OP_EQ:
                    ival = (l_float == r_float);
                    break;
                case BINARY_OP_GE:
                    ival = (l_float >= r_float);
                    break;
                case BINARY_OP_LE:
                    ival = (l_float <= r_float);
                    break;
                case BINARY_OP_NE:
                    ival = (l_float != r_float);
                    break;
                case BINARY_OP_GT:
                    ival = (l_float > r_float);
                    break;
                case BINARY_OP_LT:
                    ival = (l_float < r_float);
                    break;
                case BINARY_OP_AND:
                    ival = (l_float && r_float);
                    break;
                case BINARY_OP_OR:
                    ival = (l_float || r_float);
                    break;
            }
            EXPR(exprNode).isConstEval = 1;
            switch(EXPR(exprNode).op.binaryOp) {
                case BINARY_OP_ADD:
                case BINARY_OP_SUB:
                case BINARY_OP_MUL:
                case BINARY_OP_DIV:
                    if(datatype == INT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.iValue = (int)val;
                    else if(datatype == FLOAT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.fValue = val;
                    break;
                case BINARY_OP_EQ:
                case BINARY_OP_GE:
                case BINARY_OP_LE:
                case BINARY_OP_NE:
                case BINARY_OP_GT:
                case BINARY_OP_LT:
                case BINARY_OP_AND:
                case BINARY_OP_OR:
                    if(datatype == INT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.iValue = ival;
                    else if(datatype == FLOAT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.fValue = (float)ival;
                    break;
            }
        }
    }
    else if(EXPR(exprNode).kind == UNARY_OPERATION){
        AST_NODE *lexpr = exprNode->child;
        int l_int;
        float l_float;
        DATA_TYPE l_datatype = getExprOrConstValue(lexpr, &l_int, &l_float);
        if(l_datatype == ERROR_TYPE){
            /* error message? */
            return;
        }
        if(l_datatype == INT_TYPE){
            int val;
            switch(EXPR(exprNode).op.unaryOp){
                case UNARY_OP_POSITIVE:
                    val = l_int;
                    break;
                case UNARY_OP_NEGATIVE:
                    val = -l_int;
                    break;
                case UNARY_OP_LOGICAL_NEGATION:
                    val = !(l_int);
                    break;
            }
            EXPR(exprNode).isConstEval = 1;
            if(datatype == INT_TYPE)
                exprNode->semantic_value.exprSemanticValue.constEvalValue.iValue = val;
            else
                exprNode->semantic_value.exprSemanticValue.constEvalValue.fValue = (float)val;
        }
        else if(l_datatype == FLOAT_TYPE) {
            float val = .0;
            int ival = 0;
            switch(EXPR(exprNode).op.unaryOp) {
                case UNARY_OP_POSITIVE:
                    val = l_float;
                    break;
                case UNARY_OP_NEGATIVE:
                    val = -l_float;
                    break;
                case UNARY_OP_LOGICAL_NEGATION:
                    ival = !(l_float);
                    break;
            }
            switch(EXPR(exprNode).op.unaryOp) {
                case UNARY_OP_POSITIVE:
                case UNARY_OP_NEGATIVE:
                    if(datatype == INT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.iValue = (int)val;
                    else if(datatype == FLOAT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.fValue = val;
                    break;
                case UNARY_OP_LOGICAL_NEGATION:
                    if(datatype == INT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.iValue = ival;
                    else if(datatype == FLOAT_TYPE)
                        exprNode->semantic_value.exprSemanticValue.constEvalValue.fValue = (float)ival;
                    break;
            }
            EXPR(exprNode).isConstEval = 1;
        }
    }
}

DATA_TYPE processExprNode(AST_NODE* exprNode) {
    assert(exprNode->nodeType == EXPR_NODE);
    DATA_TYPE datatype, ldatatype, rdatatype;
    switch(EXPR(exprNode).kind){
        case BINARY_OPERATION:
            ldatatype = processExprRelatedNode(exprNode->child);
            rdatatype = processExprRelatedNode(exprNode->child->rightSibling);
            if(ldatatype == VOID_TYPE || rdatatype == VOID_TYPE){
                printErrorMsg(exprNode, VOID_OPERATION);
                return ERROR_TYPE;
            }
            if(ldatatype == CONST_STRING_TYPE || rdatatype == CONST_STRING_TYPE){
                /* error: type error */
                printErrorMsg(exprNode, STRING_OPERATION);
                return ERROR_TYPE;
            }
            if(ldatatype == ERROR_TYPE || rdatatype == ERROR_TYPE) {
                return ERROR_TYPE;
            }

            switch(EXPR(exprNode).op.binaryOp){
                /* ----- expr ----- */
                case BINARY_OP_ADD:
                case BINARY_OP_SUB:
                case BINARY_OP_MUL:
                case BINARY_OP_DIV:
                    datatype = getBiggerType(ldatatype, rdatatype);
                    evaluateExprValue(exprNode, datatype);
                    break;

                /* ----- relop_expr ----- */
                case BINARY_OP_OR:
                case BINARY_OP_AND:
                case BINARY_OP_EQ:
                case BINARY_OP_GE:
                case BINARY_OP_LE:
                case BINARY_OP_NE:
                case BINARY_OP_GT:
                case BINARY_OP_LT:
                    datatype = INT_TYPE;
                    evaluateExprValue(exprNode, datatype);
                    break;
            }
            break;
        case UNARY_OPERATION:
            /* negation or logical negation */
            datatype = processExprRelatedNode(exprNode->child);
            if(datatype != INT_TYPE && datatype != FLOAT_TYPE){
                /* error: invalid type */
                datatype = ERROR_TYPE;
                break;
            }
            if(EXPR(exprNode).op.unaryOp == UNARY_OP_LOGICAL_NEGATION){
                datatype = INT_TYPE;
            }
            evaluateExprValue(exprNode, datatype);
            break;
        default:
            printf("Invalid operation\n");
            exit(1);
    }
    exprNode->dataType = datatype;
    return datatype;
}

DATA_TYPE processVariableLValue(AST_NODE* idNode) {
    SymbolTableEntry *id_entry = retrieveSymbol(getIdByNode(idNode));
    if(id_entry == NULL){
        printErrorMsg(idNode, SYMBOL_UNDECLARED);
        return ERROR_TYPE;
    }

    SymbolAttribute *attr = id_entry->attribute;
    if(attr->attributeKind != VARIABLE_ATTRIBUTE){
        /* error: symbol is not variable */
        if(attr->attributeKind == FUNCTION_SIGNATURE)
            printErrorMsg(idNode, IS_FUNCTION_NOT_VARIABLE);
        else if(attr->attributeKind == TYPE_ATTRIBUTE)
            printErrorMsg(idNode, IS_TYPE_NOT_VARIABLE);
        return ERROR_TYPE;
    }

    TypeDescriptor *desc = attr->attr.typeDescriptor;
    switch(desc->kind){
        case SCALAR_TYPE_DESCRIPTOR:
            if(idNode->semantic_value.identifierSemanticValue.kind == ARRAY_ID) {
                printErrorMsg(idNode, NOT_ARRAY);
                return ERROR_TYPE;
            }
            return desc->properties.dataType;
        case ARRAY_TYPE_DESCRIPTOR:
            if(idNode->semantic_value.identifierSemanticValue.kind != ARRAY_ID) {
                printErrorMsg(idNode, INCOMPATIBLE_ARRAY_DIMENSION);
                return ERROR_TYPE;
            }
            int refDim = processDeclDimList(idNode->child, NULL, 0);
            if(refDim > desc->properties.arrayProperties.dimension) {
                printErrorMsg(idNode, INCOMPATIBLE_ARRAY_DIMENSION);
                return ERROR_TYPE;
            } else if(refDim < desc->properties.arrayProperties.dimension) {
                printErrorMsg(idNode, INCOMPATIBLE_ARRAY_DIMENSION);
                return ERROR_TYPE;

            }
            return desc->properties.arrayProperties.elementType;
    }
    return ERROR_TYPE;
}

DATA_TYPE processConstValueNode(AST_NODE* constValueNode) {
    switch(constValueNode->semantic_value.const1->const_type){
        case INTEGERC:
            return INT_TYPE;
        case FLOATC:
            return FLOAT_TYPE;
        case STRINGC:
            return CONST_STRING_TYPE;
    }
    return ERROR_TYPE;
}


void checkReturnStmt(AST_NODE* returnNode) {
    AST_NODE* funcNameNode = g_currentFunctionDecl->child->rightSibling;
    SymbolTableEntry *entry = retrieveSymbol(funcNameNode->semantic_value.identifierSemanticValue.identifierName);
    FunctionSignature *sign = entry->attribute->attr.functionSignature;
    switch(returnNode->child->nodeType) {
        case NUL_NODE:
            if(sign->returnType != VOID_TYPE)
                printErrorMsg(funcNameNode, RETURN_TYPE_UNMATCH);
            break;
        case IDENTIFIER_NODE:
        case CONST_VALUE_NODE:
        case EXPR_NODE: {
            DATA_TYPE retType = processExprRelatedNode(returnNode->child);
            if(retType != sign->returnType) {
                if((retType == INT_TYPE && sign->returnType == FLOAT_TYPE)
                    || (retType == FLOAT_TYPE && sign->returnType == INT_TYPE))
                    printWarnMsg(funcNameNode, IMPLICIT_CONVERSION);
                else
                    printErrorMsg(funcNameNode, RETURN_TYPE_UNMATCH);
            }
            break;
        }
        default :
            FATAL("Broken AST!");
    }
}

void processBlockNode(AST_NODE* blockNode) {
    assert(blockNode->nodeType == BLOCK_NODE);
    FOR_SIBLINGS(block, blockNode->child) {
        switch(block->nodeType){
            case VARIABLE_DECL_LIST_NODE:
                processVariableDeclListNode(block);
                break;
            case STMT_LIST_NODE:
                FOR_SIBLINGS(child, block->child)
                    processStmtNode(child);
                break;
            default:
                printf("unexpected block type\n");
                break;
        }
    }
}

void processStmtNode(AST_NODE* stmtNode)
{
    switch(stmtNode->nodeType){
        case BLOCK_NODE:
            processBlockNode(stmtNode);
            return;
        case NUL_NODE:
            return;
        case STMT_NODE:
            switch(STMT(stmtNode).kind){
                case WHILE_STMT:
                    checkWhileStmt(stmtNode);
                    break;
                case FOR_STMT:
                    checkForStmt(stmtNode);
                    break;
                case ASSIGN_STMT:
                    processExprRelatedNode(stmtNode);
                    break;
                case IF_STMT:
                    checkIfStmt(stmtNode);
                    break;
                case FUNCTION_CALL_STMT:
                    checkFunctionCall(stmtNode);
                    break;
                case RETURN_STMT:
                    checkReturnStmt(stmtNode);
                    break;
                default:
                    printf("unexpected stmt type\n");
                    break;
            }
            break;
        default:
            printf("unexpected node type\n");
            break;
    }
}

int processDeclDimList(AST_NODE* idNode, TypeDescriptor* desc, int ignoreFirstDimSize)
{
    int dim = 0;
    AST_NODE* nameNode = idNode->parent;
    while(idNode){
        if(idNode->nodeType == NUL_NODE){
            if(ignoreFirstDimSize){
                if(desc != NULL){
                    desc->properties.arrayProperties.sizeInEachDimension[dim] = -1;
                }
            }
        }
        else{
            int size;
            if(idNode->nodeType == CONST_VALUE_NODE){
                if(idNode->semantic_value.const1->const_type != INTEGERC){
                    printErrorMsg(nameNode, ARRAY_SIZE_NOT_INT);
                    size = -2;
                }
                else{
                    size = idNode->semantic_value.const1->const_u.intval;
                }
            }
            else if(idNode->nodeType == EXPR_NODE){
                if(processExprRelatedNode(idNode) != INT_TYPE){
                    printErrorMsg(nameNode, ARRAY_SIZE_NOT_INT);
                    size = -2;
                }
                else{
                    size = idNode->semantic_value.exprSemanticValue.constEvalValue.iValue;
                }
            }
            /* function call */
            else if(idNode->nodeType == STMT_NODE){
                SymbolTableEntry *entry = retrieveSymbol(getIdByNode(idNode->child));
                if(entry->attribute->attr.functionSignature->returnType != INT_TYPE)
                    printErrorMsg(nameNode, ARRAY_SUBSCRIPT_NOT_INT);
            }
            else if(idNode->nodeType == IDENTIFIER_NODE) {
                SymbolTableEntry *entry = retrieveSymbol(getIdByNode(idNode));
                if(entry->attribute->attributeKind == TYPE_ATTRIBUTE)
                    printErrorMsg(idNode, IS_TYPE_NOT_VARIABLE);
                else if(entry->attribute->attributeKind == FUNCTION_SIGNATURE)
                    printErrorMsg(idNode, IS_FUNCTION_NOT_VARIABLE);
                else {
                    TypeDescriptor *desc = entry->attribute->attr.typeDescriptor;
                    switch(desc->kind) {
                        case SCALAR_TYPE_DESCRIPTOR:
                            if(desc->properties.dataType != INT_TYPE)
                                printErrorMsg(nameNode, ARRAY_SUBSCRIPT_NOT_INT);
                            break;
                        case ARRAY_TYPE_DESCRIPTOR:
                            if(desc->properties.arrayProperties.elementType != INT_TYPE)
                                printErrorMsg(nameNode, ARRAY_SUBSCRIPT_NOT_INT);
                            else if(processDeclDimList(idNode->child, NULL, 0) != desc->properties.arrayProperties.dimension)
                                printErrorMsg(nameNode, INCOMPATIBLE_ARRAY_DIMENSION);
                            break;
                    }
                }
            }
            if(desc != NULL){
                desc->properties.arrayProperties.sizeInEachDimension[dim] = size;
            }
        }
        dim += 1;
        ignoreFirstDimSize = 0;
        idNode = idNode->rightSibling;
        if(dim >= MAX_ARRAY_DIMENSION) {
            printErrorMsg(nameNode, EXCESSIVE_ARRAY_DIM_DECLARATION);
            break;
        }
    }
    if(desc != NULL) {
        desc->properties.arrayProperties.dimension = dim;
    }
    return dim;
}


void declareFunction(AST_NODE* declarationNode)
{
    assert(declarationNode->nodeType == DECLARATION_NODE);
    assert(DECL(declarationNode).kind == FUNCTION_DECL);

    AST_NODE *ret_node, *name_node, *param_node, *block_node;
    ret_node = declarationNode->child;
    name_node = ret_node->rightSibling;
    param_node = name_node->rightSibling;
    block_node = param_node->rightSibling;

    FunctionSignature *func = (FunctionSignature*)malloc(sizeof(struct FunctionSignature));
    func->returnType = processTypeNode(ret_node);
    if(func->returnType == ERROR_TYPE){
        printErrorMsg(ret_node, SYMBOL_IS_NOT_TYPE);
        return;
    }

    SymbolAttribute* attr = newAttribute(FUNCTION_SIGNATURE);
    attr->attr.functionSignature = func;
    if(enterSymbol(getIdByNode(name_node), attr) == NULL){
        printErrorMsg(name_node, SYMBOL_REDECLARE);
        return;
    }

    openScope();
    g_currentFunctionDecl = declarationNode;

    
    func->parametersCount = 0;
    Parameter *new_param = NULL, *last_param = NULL;
    FOR_SIBLINGS(param_list, param_node->child){
        func->parametersCount += 1;
        new_param = (Parameter*)malloc(sizeof(struct Parameter));

        AST_NODE *param_type_node = param_list->child;
        AST_NODE *param_id_node = param_type_node->rightSibling;
        DATA_TYPE param_type = processTypeNode(param_type_node);
        if(param_type == ERROR_TYPE){
            printErrorMsg(param_type_node, SYMBOL_IS_NOT_TYPE);
            /* break? continue? */
        } else if(param_type == VOID_TYPE) {
            printErrorMsg(param_id_node, VOID_VARIABLE);
        }
        TypeDescriptor *desc;
        switch(param_id_node->semantic_value.identifierSemanticValue.kind){
            case NORMAL_ID:
                desc = newTypeDesc(SCALAR_TYPE_DESCRIPTOR);
                desc->properties.dataType = param_type;
                attr = newAttribute(VARIABLE_ATTRIBUTE);
                attr->attr.typeDescriptor = desc;

                if(enterSymbol(getIdByNode(param_id_node), attr) == NULL){
                    printErrorMsg(param_id_node, SYMBOL_REDECLARE);
                    break;
                }
                new_param->type = desc;
                new_param->parameterName = getIdByNode(param_id_node);
                break;

            case ARRAY_ID:
                desc = newTypeDesc(ARRAY_TYPE_DESCRIPTOR);
                desc->properties.arrayProperties.elementType = param_type;
                processDeclDimList(param_id_node->child, desc, 1);
                attr = newAttribute(VARIABLE_ATTRIBUTE);
                attr->attr.typeDescriptor = desc;

                if(enterSymbol(getIdByNode(param_id_node), attr) == NULL){
                    printErrorMsg(param_id_node, SYMBOL_REDECLARE);
                    break;
                }
                new_param->type = desc;
                new_param->parameterName = getIdByNode(param_id_node);
                break;
            default:
                printf("Unexpected kind of variable\n");
                break;
        }
        new_param->next = NULL;

        if(last_param == NULL)
            func->parameterList = new_param;
        else
            last_param->next = new_param;
        last_param = new_param;
    }
    processBlockNode(block_node);
    closeScope();
    g_currentFunctionDecl = NULL;
    return;
}

void dumpNode(AST_NODE *node) {
    printf("====START====\n");
    switch(node->nodeType) {
    case EXPR_NODE:
        dumpExprNode(node);
        break;
    case CONST_VALUE_NODE:
        dumpConstNode(node);
        break;
    case STMT_NODE:
        dumpStmtNode(node);
        break;
    case IDENTIFIER_NODE:
        dumpIdNode(node);
        break;
    case NUL_NODE:
        printf("Nul Node\n");
        break;
    case PROGRAM_NODE:
        printf("Program Node\n");
        break;
    case DECLARATION_NODE:
        printf("Declaration Node\n");
        break;
    case BLOCK_NODE:
        printf("Block Node\n");
        break;
    case NONEMPTY_ASSIGN_EXPR_LIST_NODE:
        printf("Nonempty Assign Expr List Node\n");
        break;
    case NONEMPTY_RELOP_EXPR_LIST_NODE:
        printf("Nonempty Relop Expr List Node\n");
        break;
    default:
        break;
    }
    printf("=====END=====\n");
}

void dumpExprNode(AST_NODE *node) {
    printf("Expr Node\n");

    printf("Operation Kind : %s\n", EXPR(node).kind == BINARY_OPERATION ? "Binary" : "Unary");
    switch(EXPR(node).op.binaryOp) {
        case BINARY_OP_ADD:
            printf("Operator : +\n");
            break;
        case BINARY_OP_MUL:
            printf("Operator : *\n");
            break;
        case BINARY_OP_SUB:
            printf("Operator : -\n");
            break;
        case BINARY_OP_DIV:
            printf("Operator : /\n");
            break;
        default:
            break;
    }
    switch(node->dataType) {
        case INT_TYPE:
            printf("Type : INT_TYPE\n");
            if(EXPR(node).isConstEval) {
                printf("Value : %d\n", EXPR(node).constEvalValue.iValue);
            }
            break;
        case FLOAT_TYPE:
            printf("Type : FLOAT_TYPE\n");
            if(EXPR(node).isConstEval) {
                printf("Value : %lf\n", EXPR(node).constEvalValue.fValue);
            }
            break;
        case VOID_TYPE:
            printf("Type : VOID_TYPE\n");
            break;
        case INT_PTR_TYPE:
            printf("Type : INT_PTR_TYPE\n");
            break;
        case FLOAT_PTR_TYPE:
            printf("Type : FLOAT_PTR_TYPE\n");
            break;
        case CONST_STRING_TYPE:
            printf("Type : CONST_STRING_TYPE\n");
            break;
        case NONE_TYPE:
            printf("Type : NONE_TYPE\n");
            break;
        case ERROR_TYPE:
            printf("Type : ERROR_TYPE\n");
            break;
    }
}


void dumpConstNode(AST_NODE *node) {
    printf("Const Node\n");
    CON_Type *con = node->semantic_value.const1;
    switch(con->const_type) {
        case INTEGERC:
            printf("Type : INTEGERC\n");
            printf("Value : %d\n", con->const_u.intval);
            break;
        case FLOATC:
            printf("Type : FLOATC\n");
            printf("Value : %lf\n", con->const_u.fval);
            break;
        case STRINGC:
            printf("Type : STRINGC\n");
            printf("Value : %s\n", con->const_u.sc);
            break;
        default:
            break;
    }
}

void dumpStmtNode(AST_NODE *node) {
    printf("Stmt Node\n");
    switch(STMT(node).kind) {
        case ASSIGN_STMT:
            printf("Kind : ASSIGN_STMT\n");
            break;
        case RETURN_STMT:
            printf("Kind : RETURN_STMT\n");
            break;
        default: break;
    }
}

void dumpIdNode(AST_NODE *node) {
    printf("Id Node\n");
    printf("Name : %s\n", ID(node).identifierName);
    switch(ID(node).kind) {
        case NORMAL_ID:
            printf("Kind : NORMAL_ID\n");
            break;
        case ARRAY_ID:
            printf("Kind : ARRAY_ID\n");
            break;
        case WITH_INIT_ID:
            printf("Kind : WITH_INIT_ID\n");
            break;
    }
}
