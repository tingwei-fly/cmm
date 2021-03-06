#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "header.h"
#include "symbolTable.h"

/* code generation macros */
#define GEN_CODE(fmt, ...)            { fprintf(output, "\t" fmt "\n", ##__VA_ARGS__); }
#define GEN_LABEL(name, ...)          { fprintf(output, name ":\n" , ##__VA_ARGS__); }
#define __GEN_LITERAL_int(val)        { fprintf(output, "\t_CONST_%d: .word %d\n", _const, val); }
#define __GEN_LITERAL_float(val)      { fprintf(output, "\t_CONST_%d: .float %f\n", _const, val); }
#define __GEN_LITERAL_str(val)        { fprintf(output, "\t_CONST_%d: .string %s\n", _const, val); }
#define GEN_LITERAL(type, val)        { _const++; __GEN_LITERAL_##type(val); }
#define __GEN_GLOBAL_int(name, val)   { fprintf(output, "\t__g_%s: .word %d\n", name, val); }
#define __GEN_GLOBAL_float(name, val) { fprintf(output, "\t__g_%s: .float %f\n", name, val); }
#define __GEN_GLOBAL_array(name, val) { fprintf(output, "\t__g_%s: .zero %d\n", name, val); }
#define GEN_GLOBAL(type, name, val)   { __GEN_GLOBAL_##type(name, val); }
#define ALIGN(size)                   { fprintf(output, "\t.align %d\n", size); }
#define GEN_OP(type, op, d, n, m) { \
    GEN_CODE("%1$s"#op"  %2$c%3$d, %2$c%4$d, %2$c%5$d", \
        type == INT_TYPE ? "" : "f", \
        type == INT_TYPE ? 'w' : 's', \
        d, n, m); \
}

/* semantic macros */
#define CONST_TYPE(node)        (node)->semantic_value.const1->const_type
#define __CONST_OF_int(node)    (node)->semantic_value.const1->const_u.intval
#define __CONST_OF_float(node)  (node)->semantic_value.const1->const_u.fval
#define __CONST_OF_str(node)    (node)->semantic_value.const1->const_u.sc
#define CONST_VAL(node, type)   __CONST_OF_##type(node)

#define SIGN(node)        ENTRY(node)->attribute->attr.functionSignature
#define DESC(node)        ENTRY(node)->attribute->attr.typeDescriptor
#define IS_VAR(node)      (ENTRY(node)->attribute->attributeKind == VARIABLE_ATTRIBUTE)
#define IS_TYPE(node)     (ENTRY(node)->attribute->attributeKind == TYPE_ATTRIBUTE)
#define IS_FUNC(node)     (ENTRY(node)->attribute->attributeKind == FUNCTION_SIGNATURE)
#define ARRAY_PROP(node)  DESC(node)->properties.arrayProperties
#define IS_SCALAR(node)   (DESC(id)->kind == SCALAR_TYPE_DESCRIPTOR)
#define IS_LOCAL(node)    (ENTRY(node)->nestingLevel > 0)
#define STMT(node)        (node)->semantic_value.stmtSemanticValue
#define EXPR(node)        (node)->semantic_value.exprSemanticValue
#define ID(node)          (node)->semantic_value.identifierSemanticValue
#define NAME(node)        (ID(node)).identifierName
#define ENTRY(node)       (ID(node)).symbolTableEntry
#define IS_NORMAL(node)   ((ID(node)).kind == NORMAL_ID)

/* Macro for accessing parameter information */
#define PARAM_DESC(param)         (param)->type
#define PARAM_IS_SCALAR(param)    (PARAM_DESC(param)->kind == SCALAR_TYPE_DESCRIPTOR)
#define PARAM_SCALAR_TYPE(param)  PARAM_DESC(param)->properties.dataType
#define PARAM_ARRAY_TYPE(param)   PARAM_DESC(param)->properties.arrayProperties.elementType
#define PARAM_DIM(param)          PARAM_DESC(param)->properties.arrayProperties.dimension
#define PARAM_DIMS(param)         PARAM_DESC(param)->properties.arrayProperties.sizeInEachDimension

#define FATAL(x) { \
    fprintf(stderr, "Fatal : " x "\nAborted.\n"); \
    exit(1); \
}

#define FOR_SIBLINGS(name, node) \
    for(AST_NODE* (name) = (node) \
        ; (name) \
        ; (name) = (name)->rightSibling)

static enum {None, Text, Data} mode = None;
#define SWITCH_TO(type) if(mode != type) { mode = type; fprintf(output, "."#type"\n"); }

static FILE *output;            /* output file */
static int _AR_offset;          /* AR size counter*/
static int _local_var_offset;   /* local variable size counter */
static int _label_count;        /* label counter */
static int _const;              /* constant counter */
static int _if_has_return;      /* use with optimization for if-else block containing return */

/* cached current function information */
static DATA_TYPE _return_type;
static char *_func_name = "";

static int _save_retval = 0;

/* for register allocation */
static int regs[32];
static int FPregs[32];

#define __CALLER_SAVED_int 9, 15
#define __CALLEE_SAVED_int 19, 28
#define __CALLER_SAVED_float 16, 31
#define __CALLEE_SAVED_float 8, 15
#define __INTERNAL_REGS_RANGE(name, START, END) int name = START; name <= END; ++name
#define __REGS_RANGE(name, ...) __INTERNAL_REGS_RANGE(name, __VA_ARGS__)
#define CALLER_SAVED(name, type) __REGS_RANGE(name, __CALLER_SAVED_##type)
#define CALLEE_SAVED(name, type) __REGS_RANGE(name, __CALLEE_SAVED_##type)
#define __INTERNAL_WALK_REG_Q(name, START, END) static int name = START; if(name > END) name = START; for(;name <= END; ++name)
#define __WALK_REG_Q(name, ...) __INTERNAL_WALK_REG_Q(name, __VA_ARGS__)
#define WALK_REG_Q(name, conv, type) __WALK_REG_Q(name, __##conv##_SAVED_##type)
#define CHECK_REG_LEAK() { \
    printf("before function %s:\n", _func_name); \
    for(int i = 0; i < 31; ++i) { \
        if(regs[i]) \
            printf("\tx%d not freed\n", i); \
    } \
    for(int i = 0; i < 31; ++i) { \
        if(FPregs[i]) \
            printf("\td%d not freed\n", i); \
    } \
}

int __allocReg_int_caller(int fallback);
int __allocReg_int_callee(int fallback);
int __allocReg_float_caller(int fallback);
int __allocReg_float_callee(int fallback);

int __allocReg_int_caller(int fallback) {
    WALK_REG_Q(qtop, CALLER, int) {
        if(!regs[qtop]) {
            regs[qtop] = 1;
            return qtop++;
        }
    }
    return fallback ? __allocReg_int_callee(0) : -1;
}

int __allocReg_int_callee(int fallback) {
    WALK_REG_Q(qtop, CALLEE, int) {
        if(!regs[qtop]) {
            regs[qtop] = 1;
            return qtop++;
        }
    }
    return fallback ? __allocReg_int_caller(0) : -1;
}

int __allocReg_float_caller(int fallback) {
    WALK_REG_Q(qtop, CALLER, float) {
        if(!FPregs[qtop]) {
            FPregs[qtop] = 1;
            return qtop++;
        }
    }
    return fallback ? __allocReg_int_callee(0) : -1;
}

int __allocReg_float_callee(int fallback) {
    WALK_REG_Q(qtop, CALLEE, float) {
        if(!FPregs[qtop]) {
            FPregs[qtop] = 1;
            return qtop++;
        }
    }
    return fallback ? __allocReg_int_caller(0) : -1;
}

#define allocReg(type, prefer) __allocReg_##type##_##prefer(1)

static __inline__ void __freeReg_int(int reg) { regs[reg] = 0; }
static __inline__ void __freeReg_float(int reg) { FPregs[reg] = 0; }
#define freeReg(reg, type) __freeReg_##type(reg)

static __inline__ void genWrite(int reg, DATA_TYPE type) {
    GEN_CODE("%1$smov %2$c0, %2$c%3$d",
        type == FLOAT_TYPE ? "f" : "",
        type == FLOAT_TYPE ? 's' : type == INT_TYPE ? 'w' : 'x',
        reg);
    GEN_CODE("bl _write_%s",
        type == INT_TYPE ? "int" : type == FLOAT_TYPE ? "float" : "str");
}

#define GEN_FCVTZS(node, conv) { \
    int tmp = allocReg(int, conv); \
    GEN_CODE("fcvtzs w%d, s%d", tmp, node->place); \
    freeReg(node->place, float); \
    node->place = tmp; \
    node->dataType = INT_TYPE; \
}

#define GEN_SCVTF(node, conv) { \
    int tmp = allocReg(float, conv); \
    GEN_CODE("scvtf s%d, w%d", tmp, node->place); \
    freeReg(node->place, int); \
    node->place = tmp; \
    node->dataType = FLOAT_TYPE; \
}

struct {
    DATA_TYPE type;
    int place;
    int offset;
} _param_info[100];
int _param_stack_top = 0;


void genAssignOrExpr(AST_NODE *node);
void genExprRelatedNode(AST_NODE *node);
void genExprNode(AST_NODE *node);
void genFunctionCall(AST_NODE *node);
void genAssignmentStmt(AST_NODE *node);
void genBlockNode(AST_NODE *node);
void genForStmt(AST_NODE *node);
void genWhileStmt(AST_NODE *node);
void genReturnStmt(AST_NODE *_node);
void genIfStmt(AST_NODE *node);
void genStmtNode(AST_NODE *node);
void genStatementList(AST_NODE* node);
void genLocalDeclaration(AST_NODE* node);
void genLocalDeclarations(AST_NODE* node);
void genGlobalDeclration(AST_NODE *node);
void genGlobalDeclrations(AST_NODE *node);
void genProgramNode(AST_NODE *_node);
void genFunctionDeclration(AST_NODE *node);
int genArrayRef(AST_NODE *node);
void genVariableRvalue(AST_NODE *node);
void genConstValueNode(AST_NODE *node);
void genGeneralNode(AST_NODE *node);
void genPrologue();
void genEpilogue();

int genSaveCallerRegs();
void genRestoreCallerRegs();
int evalParameter(Parameter* param, AST_NODE* relop);

void genGeneralNode(AST_NODE *node) {
    switch(node->nodeType) {
        case VARIABLE_DECL_LIST_NODE:
            genLocalDeclarations(node);
            break;
        case STMT_LIST_NODE:
            genStatementList(node);
            break;
        case NONEMPTY_ASSIGN_EXPR_LIST_NODE:
            FOR_SIBLINGS(child, node->child)
                genAssignOrExpr(child);
            break;
        case NONEMPTY_RELOP_EXPR_LIST_NODE:
            FOR_SIBLINGS(child, node->child)
                genExprRelatedNode(child);
            break;
        case NUL_NODE:
        default:
            break;
    }
}

void genLocalDeclarations(AST_NODE* node) {
    FOR_SIBLINGS(decl_node, node->child)
        genLocalDeclaration(decl_node);
}

void genLocalDeclaration(AST_NODE* node) {
    AST_NODE *type_node = node->child;
    FOR_SIBLINGS(id, type_node->rightSibling) {
        SymbolTableEntry *entry = ENTRY(id);
        entry->is_pointer = 0;
        if(IS_SCALAR(id)) {
            _local_var_offset -= 4;
            entry->offset = _local_var_offset;
            if(ID(id).kind == WITH_INIT_ID) {
                AST_NODE *relop = id->child;
                genExprRelatedNode(relop);
                GEN_CODE("str %c%d, [x29, #%d]",
                    type_node->dataType == INT_TYPE ? 'w' : 's',
                    relop->place, entry->offset);
                if(type_node->dataType == INT_TYPE)
                    freeReg(relop->place, int);
                else
                    freeReg(relop->place, float);
            }
        } else {
            int size = 4;
            for(int dim = 0 ; dim < ARRAY_PROP(id).dimension; ++dim) {
                size *= ARRAY_PROP(id).sizeInEachDimension[dim];
            }
            _local_var_offset -= size;
            entry->offset = _local_var_offset;
        }
    }
}

void genExprRelatedNode(AST_NODE *node) {
    switch(node->nodeType) {
        case EXPR_NODE:
            genExprNode(node);
            break;
        case STMT_NODE:
            genFunctionCall(node);
            break;
        case IDENTIFIER_NODE:
            genVariableRvalue(node);
            break;
        case CONST_VALUE_NODE:
            genConstValueNode(node);
            break;
        default:
            break;
    }
}

void genAssignOrExpr(AST_NODE *node) {
    if(node->nodeType == STMT_NODE) {
        if(STMT(node).kind == ASSIGN_STMT)
            genAssignmentStmt(node);
        else if(STMT(node).kind == FUNCTION_CALL_STMT)
            genFunctionCall(node);
    } else {
        genExprRelatedNode(node);
    }
}

void genFunctionCall(AST_NODE *node) {
    node->place = 0;
    char *func_name = NAME(node->child);
    AST_NODE *relop_expr = node->child->rightSibling;
    if(!strcmp(func_name, "write")) {
        genExprRelatedNode(relop_expr->child);
        relop_expr->place = relop_expr->child->place;
        DATA_TYPE type = relop_expr->child->dataType;
        genWrite(relop_expr->place, type);
        if(type == FLOAT_TYPE)
            freeReg(relop_expr->place, float);
        else
            freeReg(relop_expr->place, int);
    } else if(SIGN(node->child)->parametersCount > 0) {
        DATA_TYPE return_type = SIGN(node->child)->returnType;
        if(_save_retval) {
            if(return_type == INT_TYPE)
                node->place = allocReg(int, callee);
            else if(return_type == FLOAT_TYPE)
                node->place = allocReg(float, callee);
        }
        /* eval params */
        genGeneralNode(relop_expr);
        /* pass args */
        Parameter *param = SIGN(node->child)->parameterList;
        /* type conversion occurs here */
        int param_size = evalParameter(param, relop_expr);
        /* pre-free the regs holding arguments so they won't be saved in genSaveCallerRegs() */
        for(int i = _param_stack_top - 1; i >= 0; --i) {
            if(_param_info[i].type == FLOAT_TYPE)
                freeReg(_param_info[i].place, float);
            else /* INT_TYPE, *_PTR_TYPE */
                freeReg(_param_info[i].place, int);
        }
        int caller_size = genSaveCallerRegs() * 8;
        /* generate param store instructions */
        for(int i = _param_stack_top - 1; i >= 0; --i) {
            if(_param_info[i].type == FLOAT_TYPE) {
                GEN_CODE("str d%d, [sp, #%d]",
                    _param_info[i].place, _param_info[i].offset - caller_size);
            } else { /* INT_TYPE, *_PTR_TYPE */
                GEN_CODE("str x%d, [sp, #%d]",
                    _param_info[i].place, _param_info[i].offset - caller_size);
            }
        }
        int size = caller_size + param_size;
        GEN_CODE("sub sp, sp, #%d", size);
        GEN_CODE("bl _start_%s", func_name);
        /* restore sp space for args and saved regs*/
        GEN_CODE("add sp, sp, #%d", size);
        if(caller_size)
            genRestoreCallerRegs();
        if(_save_retval) {
            if(return_type == INT_TYPE) {
                GEN_CODE("mov w%d, w0", node->place);
            } else if(return_type == FLOAT_TYPE) {
                GEN_CODE("fmov s%d, s0", node->place);
            }
        }
    } else if(!strcmp(func_name, SYMBOL_TABLE_SYS_LIB_READ)) {
        GEN_CODE("bl _read_int");
    } else if(!strcmp(func_name, SYMBOL_TABLE_SYS_LIB_FREAD)) {
        GEN_CODE("bl _read_float");
    } else {
        DATA_TYPE return_type = SIGN(node->child)->returnType;
        if(_save_retval) {
            if(return_type == INT_TYPE)
                node->place = allocReg(int, callee);
            else
                node->place = allocReg(float, callee);
        }
        int save_count = genSaveCallerRegs();
        if(save_count)
            GEN_CODE("sub sp, sp, #%d", 8 * save_count);
        GEN_CODE("bl _start_%s", func_name);
        if(save_count) {
            GEN_CODE("add sp, sp, #%d", 8 * save_count);
            genRestoreCallerRegs();
        }
        if(_save_retval) {
            if(return_type == INT_TYPE) {
                GEN_CODE("mov w%d, w0", node->place);
            } else if(return_type == FLOAT_TYPE) {
                GEN_CODE("mov s%d, s0", node->place);
            }
        }
    }
}

void genBlockNode(AST_NODE *node) {
    FOR_SIBLINGS(child, node->child)
        genGeneralNode(child);
}

void genStmtNode(AST_NODE *node) {
    if(node->nodeType == NUL_NODE) {
        return;
    } else if(node->nodeType == BLOCK_NODE) {
        genBlockNode(node);
    } else {
        switch(STMT(node).kind) {
            case WHILE_STMT:
                genWhileStmt(node);
                break;
            case FOR_STMT:
                genForStmt(node);
                break;
            case ASSIGN_STMT:
                genAssignmentStmt(node);
                break;
            case IF_STMT:
                genIfStmt(node);
                break;
            case FUNCTION_CALL_STMT:
                genFunctionCall(node);
                break;
            case RETURN_STMT:
                genReturnStmt(node);
                break;
        }
    }
}

void genStatementList(AST_NODE* node) {
    FOR_SIBLINGS(stmt, node->child)
        genStmtNode(stmt);
}

void genGlobalDeclration(AST_NODE *node) {
    AST_NODE *type_node = node->child;
    FOR_SIBLINGS(id, type_node->rightSibling) {
        if(IS_TYPE(id))
            continue;
        if(IS_SCALAR(id)) {
            if(ID(id).kind == WITH_INIT_ID) {
                if(type_node->dataType == INT_TYPE) {
                    if(id->child->nodeType == EXPR_NODE
                       && EXPR(id->child).isConstEval) {
                        GEN_GLOBAL(int, NAME(id), EXPR(id->child).constEvalValue.iValue);
                    } else if(id->child->nodeType == CONST_VALUE_NODE) {
                        GEN_GLOBAL(int, NAME(id), CONST_VAL(id->child, int));
                    }
                } else {
                    if(id->child->nodeType == EXPR_NODE
                       && EXPR(id->child).isConstEval) {
                        GEN_GLOBAL(float, NAME(id), EXPR(id->child).constEvalValue.fValue);
                    } else if(id->child->nodeType == CONST_VALUE_NODE) {
                        GEN_GLOBAL(float, NAME(id), CONST_VAL(id->child, float));
                    }
                }
            } else {
                if(type_node->dataType == INT_TYPE) {
                    GEN_GLOBAL(int, NAME(id), 0);
                } else{
                    GEN_GLOBAL(float, NAME(id), .0);
                }
            }
        } else {
            int size = 4;
            for(int dim = 0 ; dim < ARRAY_PROP(id).dimension; ++dim) {
                size *= ARRAY_PROP(id).sizeInEachDimension[dim];
            }
            GEN_GLOBAL(array, NAME(id), size);
        }
        ENTRY(id)->is_pointer = 0;
    }
}

void genReturnStmt(AST_NODE *node) {
    _if_has_return = 1;
    if(_return_type == VOID_TYPE)
        return ;

    AST_NODE *val = node->child;
    genExprRelatedNode(val);
    if(_return_type != val->dataType) {
        if(_return_type == INT_TYPE) {
            GEN_FCVTZS(val, callee);
        } else {
            GEN_SCVTF(val, callee);
        }
    }
    if(_return_type == INT_TYPE)
        freeReg(val->place, int);
    else
        freeReg(val->place, float);

    GEN_CODE("%1$smov %2$c0, %2$c%3$d",
        _return_type == INT_TYPE ? "" : "f",
        _return_type == INT_TYPE ? 'w' : 's',
        val->place);
    GEN_CODE("b _end_%s", _func_name);
}

void genExprNode(AST_NODE *node) {
    if(EXPR(node).isConstEval) {
        if(node->dataType == INT_TYPE) {
            node->place = allocReg(int, callee);
            GEN_CODE("mov w%d, #%d", node->place, EXPR(node).constEvalValue.iValue);
        }
        else if(node->dataType == FLOAT_TYPE) {
            node->place = allocReg(float, callee);
            SWITCH_TO(Data);
            GEN_LITERAL(float, EXPR(node).constEvalValue.fValue);
            SWITCH_TO(Text);
            GEN_CODE("ldr s%d, _CONST_%d", node->place, _const);
        }
        return ;
    }

    int local_save_retval = _save_retval;
    _save_retval = 1;
    if(EXPR(node).kind == UNARY_OPERATION) {
        genExprRelatedNode(node->child);
        node->place = node->child->place;
        switch(EXPR(node).op.unaryOp) {
            case UNARY_OP_NEGATIVE:
                GEN_CODE("%1$sneg %2$c%3$d, %2$c%3$d",
                    node->dataType == INT_TYPE ? "" : "f",
                    node->dataType == INT_TYPE ? 'w' : 's',
                    node->child->place);
                break;
            case UNARY_OP_LOGICAL_NEGATION:
                GEN_CODE("%scmp %c%d, #0",
                    node->child->dataType == INT_TYPE ? "" : "f",
                    node->child->dataType == INT_TYPE ? 'w' : 's',
                    node->child->place);
                if(node->child->dataType == FLOAT_TYPE) {
                    node->place = allocReg(int, callee);
                    freeReg(node->child->place, float);
                    node->dataType = INT_TYPE;
                }
                GEN_CODE("cset w%d, EQ", node->place);
            default:
                break;
        }
    } else {
        AST_NODE *lhs = node->child;
        AST_NODE *rhs = lhs->rightSibling;
        genExprRelatedNode(lhs);
        switch(EXPR(node).op.binaryOp) {
            /* delay eval of rhs for short-circuit logical ops */
            case BINARY_OP_ADD ... BINARY_OP_LT:
                genExprRelatedNode(rhs);
                break;
            default: break;
        }

        DATA_TYPE logical_op_type;
        if(lhs->dataType != rhs->dataType) {
            if(EXPR(node).op.binaryOp != BINARY_OP_AND
               && EXPR(node).op.binaryOp != BINARY_OP_OR) {
                if(lhs->dataType == INT_TYPE) {
                    GEN_SCVTF(lhs, callee);
                } else {
                    GEN_SCVTF(rhs, callee);
                }
            } else {
                /* reuse reg of int side */
                if(lhs->dataType == INT_TYPE)
                    node->place = lhs->place;
                else
                    node->place = rhs->place;
            }
        }
        if(lhs->dataType == rhs->dataType && lhs->dataType == FLOAT_TYPE) {
            switch(EXPR(node).op.binaryOp) {
                /* EQ, GE, LE, NE, GT, LT, AND, OR produce int result
                 * we need an integer register for result
                 * */
                case BINARY_OP_EQ ... BINARY_OP_OR:
                    logical_op_type = FLOAT_TYPE;
                    node->dataType = INT_TYPE;
                    node->place = allocReg(int, callee);
                    break;
                /* use lhs as result for arithmetic ops */
                default:
                    node->place = lhs->place;
                    node->dataType = FLOAT_TYPE;
                    break;
            }
        } else if(lhs->dataType == rhs->dataType) {
            /* lhs & rhs are int, use lhs as destination */
            logical_op_type = INT_TYPE;
            node->dataType = lhs->dataType;
            node->place = lhs->place;
        }

        /* now operands will be both float or int, except AND & OR */
        switch(EXPR(node).op.binaryOp) {
            case BINARY_OP_ADD ... BINARY_OP_DIV:
                if(EXPR(node).op.binaryOp == BINARY_OP_ADD) {
                    GEN_OP(node->dataType, add, lhs->place, lhs->place, rhs->place);
                } else if(EXPR(node).op.binaryOp == BINARY_OP_SUB) {
                    GEN_OP(node->dataType, sub, lhs->place, lhs->place, rhs->place);
                } else if(EXPR(node).op.binaryOp == BINARY_OP_MUL) {
                    GEN_OP(node->dataType, mul, lhs->place, lhs->place, rhs->place);
                } else if(EXPR(node).op.binaryOp == BINARY_OP_DIV) {
                    if(node->dataType == INT_TYPE) {
                        GEN_CODE("sdiv w%d, w%d, w%d", lhs->place, lhs->place, rhs->place);
                    } else {
                        GEN_CODE("fdiv s%d, s%d, s%d", lhs->place, lhs->place, rhs->place);
                    }
                }

                if(node->dataType == INT_TYPE)
                    freeReg(rhs->place, int);
                else
                    freeReg(rhs->place, float);
                break;
                /* EQ, GE, LE, NE, GT, LT*/
            case BINARY_OP_EQ ... BINARY_OP_LT:
                GEN_CODE("%1$scmp %2$c%3$d, %2$c%4$d",
                    logical_op_type == INT_TYPE ? "" : "f",
                    logical_op_type == INT_TYPE ? 'w' : 's',
                    lhs->place, rhs->place);

                GEN_CODE("cset w%d, %s", node->place,
                    EXPR(node).op.binaryOp == BINARY_OP_EQ ? "eq" : \
                    EXPR(node).op.binaryOp == BINARY_OP_GE ? "ge" : \
                    EXPR(node).op.binaryOp == BINARY_OP_LE ? "le" : \
                    EXPR(node).op.binaryOp == BINARY_OP_NE ? "ne" : \
                    EXPR(node).op.binaryOp == BINARY_OP_GT ? "gt" : "lt");

                if(lhs->dataType == FLOAT_TYPE)
                    freeReg(lhs->place, float);
                if(rhs->dataType == INT_TYPE)
                    freeReg(rhs->place, int);
                else
                    freeReg(rhs->place, float);
                break;
            case BINARY_OP_AND:
            case BINARY_OP_OR: {
                /* eval lhs */
                int short_circuit_label = ++_label_count;
                int end_label = ++_label_count;
                if(lhs->dataType == INT_TYPE) {
                    GEN_CODE("cb%s w%d, _L%d",
                        EXPR(node).op.binaryOp == BINARY_OP_AND ? "z" : "nz",
                        lhs->place,
                        short_circuit_label);
                } else {
                    GEN_CODE("fcmp s%d, #0", lhs->place);
                    GEN_CODE("b.%s _L%d",
                        EXPR(node).op.binaryOp == BINARY_OP_AND ? "eq" : "ne",
                        short_circuit_label);
                }

                /* eval rhs */
                genExprRelatedNode(rhs);
                if(rhs->dataType == INT_TYPE) {
                    GEN_CODE("cb%s w%d, _L%d",
                        EXPR(node).op.binaryOp == BINARY_OP_AND ? "z" : "nz",
                        rhs->place,
                        short_circuit_label);
                } else {
                    GEN_CODE("fcmp s%d, #0", rhs->place);
                    GEN_CODE("b.%s _L%d",
                        EXPR(node).op.binaryOp == BINARY_OP_AND ? "eq" : "ne",
                        short_circuit_label);
                }
                GEN_CODE("mov w%d, %d", node->place,
                    EXPR(node).op.binaryOp == BINARY_OP_AND ? 1 : 0);
                GEN_CODE("b _L%d", end_label);
                GEN_LABEL("_L%d", short_circuit_label);
                GEN_CODE("mov w%d, %d", node->place,
                    EXPR(node).op.binaryOp == BINARY_OP_AND ? 0 : 1);
                GEN_LABEL("_L%d", end_label);
                if(lhs->dataType == FLOAT_TYPE)
                    freeReg(lhs->place, float);
                else if(lhs->place != node->place)
                    freeReg(lhs->place, int);
                if(rhs->dataType == FLOAT_TYPE)
                    freeReg(rhs->place, float);
                else if(rhs->place != node->place)
                    freeReg(rhs->place, int);
                break;
            }
        }
    }
    _save_retval = local_save_retval;
}

int genArrayRef(AST_NODE *node) {
    AST_NODE *dim_list = node->child;

    /* has subscript */
    if(dim_list) {
        int offset_tmp = allocReg(int, callee);
        int addr_tmp = allocReg(int, callee);
        genExprRelatedNode(dim_list);
        GEN_CODE("mov w%d, w%d", offset_tmp, dim_list->place);
        freeReg(dim_list->place, int);
        int dim = 1;
        /* for each subsciprt */
        FOR_SIBLINGS(traverse_dim, dim_list->rightSibling) {
            int dim_size = allocReg(int, callee);
            GEN_CODE("mov w%d, #%d", 
                dim_size, ARRAY_PROP(node).sizeInEachDimension[dim-1]);
            genExprRelatedNode(traverse_dim);
            /* offset = offset * dimension size[n-1] + dimension val[n] */
            GEN_CODE("madd w%1$d, w%1$d, w%2$d, w%3$d",
                offset_tmp, dim_size, traverse_dim->place);
            freeReg(traverse_dim->place, int);
            freeReg(dim_size, int);
            ++dim;
        }

        /* if subscript doesn't match dimension, it's a pointer
         * treat remain field as 0
         */
        int dim_size = allocReg(int, callee);
        for(; dim < ARRAY_PROP(node).dimension; ++dim) {
            GEN_CODE("mov w%d, #%d",
                dim_size, ARRAY_PROP(node).sizeInEachDimension[dim-1]);
            GEN_CODE("mul w%1$d, w%1$d, w%2$d",
                offset_tmp, dim_size);
        }
        freeReg(dim_size, int);
        if(ENTRY(node)->is_pointer) {
            GEN_CODE("ldr x%d, [x29, #%d]", addr_tmp, ENTRY(node)->offset);
        } else if(IS_LOCAL(node)) {
            GEN_CODE("add x%d, x29, #%d", addr_tmp, ENTRY(node)->offset);
        } else {
            GEN_CODE("ldr x%d, =__g_%s", addr_tmp, NAME(node));
        }
        /* addr = addr + offset << 2 */
        GEN_CODE("add x%1$d, x%1$d, w%2$d, sxtw 2", addr_tmp, offset_tmp);
        freeReg(offset_tmp, int);
        freeReg(addr_tmp, int);
        return addr_tmp;
    }
    else{
        int addr_tmp = allocReg(int, callee);
        if(ENTRY(node)->is_pointer) {
            GEN_CODE("ldr x%d, [x29, #%d]", addr_tmp, ENTRY(node)->offset);
        } else if(IS_LOCAL(node)) {
            GEN_CODE("add x%d, x29, #%d", addr_tmp, ENTRY(node)->offset);
        } else {
            GEN_CODE("ldr x%d, =__g_%s", addr_tmp, NAME(node));
        }
        freeReg(addr_tmp, int);
        return addr_tmp;
    }
}

void genVariableRvalue(AST_NODE *node) {
    if(node->dataType == FLOAT_TYPE)
        node->place = allocReg(float, callee);
    else
        node->place = allocReg(int, callee);

    if(node->dataType == INT_PTR_TYPE || node->dataType == FLOAT_PTR_TYPE) {
        /* pointer (arguments) */
        int addr_tmp = genArrayRef(node);
        GEN_CODE("mov x%d, x%d", node->place, addr_tmp);
    } else if(IS_NORMAL(node)) {
        /* scalar */
        if(IS_LOCAL(node)) {
            GEN_CODE("ldr %c%d, [x29, #%d]",
                node->dataType == INT_TYPE ? 'w' : 's',
                node->place,
                ENTRY(node)->offset);
        } else {
            GEN_CODE("ldr %c%d, __g_%s",
                node->dataType == INT_TYPE ? 'w' : 's',
                node->place, NAME(node));

        }
    } else {
        /* array */
        int addr_tmp = genArrayRef(node);
        GEN_CODE("ldr %c%d, [x%d]",
            node->dataType == INT_TYPE ? 'w' : 's',
            node->place, addr_tmp);
    }
}

void genConstValueNode(AST_NODE *node) {
    if(CONST_TYPE(node) == INTEGERC) {
        node->place = allocReg(int, callee);
        GEN_CODE("mov w%d, #%d", node->place, CONST_VAL(node, int));
    } else if(CONST_TYPE(node) == FLOATC) {
        node->place = allocReg(float, callee);
        SWITCH_TO(Data);
        GEN_LITERAL(float, CONST_VAL(node, float));
        SWITCH_TO(Text);
        GEN_CODE("ldr s%d, _CONST_%d", node->place, _const);
    } else {
        node->place = allocReg(int, callee);
        SWITCH_TO(Data);
        GEN_LITERAL(str, CONST_VAL(node, str));
        ALIGN(4);
        SWITCH_TO(Text);
        GEN_CODE("ldr x%d, =_CONST_%d", node->place, _const);
    }
}

void genIfStmt(AST_NODE *node) {
    AST_NODE *cond = node->child;
    AST_NODE *if_block = cond->rightSibling;
    AST_NODE *else_block = if_block->rightSibling;

    genExprRelatedNode(cond);
    int end_label, else_label;
    if(!else_block) {
        /* no else block */
        end_label = ++_label_count;
        /* try to optimize using CBZ for INT_TYPE */
        if(cond->dataType == INT_TYPE) {
            GEN_CODE("cbz w%d, _L%d", cond->place, end_label);
            freeReg(cond->place, int);
        } else {
            GEN_CODE("fcmp s%d, #0", cond->place);
            GEN_CODE("b.eq _L%d", end_label);
            freeReg(cond->place, float);
        }

        genStmtNode(if_block);
        GEN_LABEL("_L%d", end_label);
        return ;
    }
    else_label = ++_label_count;
    end_label = ++_label_count;
    /* try to optimize using CBZ for INT_TYPE */
    if(cond->dataType == INT_TYPE) {
        GEN_CODE("cbz w%d, _L%d", cond->place, else_label);
        freeReg(cond->place, int);
    } else {
        GEN_CODE("fcmp s%d, #0", cond->place);
        GEN_CODE("b.eq _L%d", else_label);
        freeReg(cond->place, float);
    }

    _if_has_return = 0;
    genStmtNode(if_block);
    /* try to eliminate unconditional branch if block contains return
     * this checking isn't always working, need more sophiscated reachability analysis
     */
    if(!_if_has_return)
        GEN_CODE("b _L%d", end_label);
    _if_has_return = 0;
    GEN_LABEL("_L%d", else_label);
    genStmtNode(else_block);

    if(!_if_has_return)
        GEN_CODE("b _L%d", end_label);

    GEN_LABEL("_L%d", end_label);
    _if_has_return = 0;
}

void genForStmt(AST_NODE *node) {
    AST_NODE *init = node->child;
    AST_NODE *cond = init->rightSibling;
    AST_NODE *loop = cond->rightSibling;
    AST_NODE *block = loop->rightSibling;

    genGeneralNode(init);
    int start_label = ++_label_count;
    int end_label = ++_label_count;
    GEN_LABEL("_L%d", start_label);

    genGeneralNode(cond);
    AST_NODE *cond_result = cond->child;
    /* if cond contains multiple expr, we must evaluate all */
    while(cond_result) {
        if(cond_result->dataType == INT_TYPE)
            freeReg(cond_result->place, int);
        else
            freeReg(cond_result->place, float);
        if(!cond_result->rightSibling)
            break;
        cond_result = cond_result->rightSibling;
    }
    /* but only use the last expr as result, and cond-expr may be empty */
    if(cond_result) {
        /* try to optimize using CBZ for INT_TYPE */
        if(cond_result->dataType == INT_TYPE) {
            GEN_CODE("cbz w%d, _L%d", cond_result->place, end_label);
        } else {
            GEN_CODE("fcmp s%d, #0", cond_result->place);
            GEN_CODE("b.eq _L%d", end_label);
        }
    }

    genStmtNode(block);
    genGeneralNode(loop);
    GEN_CODE("b _L%d", start_label);
    GEN_LABEL("_L%d", end_label);
}

void genWhileStmt(AST_NODE *node) {
    AST_NODE *cond = node->child;
    AST_NODE *block = cond->rightSibling;

    int start_label = ++_label_count;
    int end_label = ++_label_count;

    GEN_LABEL("_L%d", start_label);
    genExprRelatedNode(cond);
    /* try to optimize using CBZ for INT_TYPE */
    if(cond->dataType == INT_TYPE) {
        GEN_CODE("cbz w%d, _L%d", cond->place, end_label);
        freeReg(cond->place, int);
    } else {
        GEN_CODE("fcmp s%d, #0", cond->place);
        GEN_CODE("b.eq _L%d", end_label);
        freeReg(cond->place, float);
    }

    genStmtNode(block);
    GEN_CODE("b _L%d", start_label);
    GEN_LABEL("_L%d", end_label);
}

void genAssignmentStmt(AST_NODE *node) {
    AST_NODE *lhs = node->child, *rhs = node->child->rightSibling;
    genExprRelatedNode(rhs);
    if(lhs->dataType != rhs->dataType) {
        if(lhs->dataType == INT_TYPE) {
            GEN_FCVTZS(rhs, caller);
        } else {
            GEN_SCVTF(rhs, caller);
        }
    }
    if(IS_NORMAL(lhs)) {
        if(IS_LOCAL(lhs)) {
            GEN_CODE("str %c%d, [x29, #%d]",
                lhs->dataType == INT_TYPE ? 'w' : 's',
                rhs->place,
                ENTRY(lhs)->offset);
        } else {
            int temp = allocReg(int, caller);
            GEN_CODE("ldr x%d, =__g_%s", temp, NAME(lhs));
            GEN_CODE("str %c%d, [x%d]",
                lhs->dataType == INT_TYPE ? 'w' : 's',
                rhs->place, temp);
            freeReg(temp, int);
        }
    } else {
        int addr_tmp = genArrayRef(lhs);
        GEN_CODE("str %c%d, [x%d]",
            lhs->dataType == INT_TYPE ? 'w' : 's',
            rhs->place, addr_tmp);
    }
    if(rhs->dataType == INT_TYPE)
        freeReg(rhs->place, int);
    else
        freeReg(rhs->place, float);
}

void genGlobalDeclrations(AST_NODE *node) {
    SWITCH_TO(Data);
    FOR_SIBLINGS(decl, node->child)
        genGlobalDeclration(decl);
}

void genProgramNode(AST_NODE *node) {
    FOR_SIBLINGS(global_decl_list, node->child) {
        if(global_decl_list->nodeType == VARIABLE_DECL_LIST_NODE)
            genGlobalDeclrations(global_decl_list);
        else if(global_decl_list->nodeType == DECLARATION_NODE)
            genFunctionDeclration(global_decl_list);
    }
}

void genFunctionDeclration(AST_NODE *node) {
    _AR_offset = 0;
    AST_NODE *name_node = node->child->rightSibling;
    _func_name = NAME(name_node);
    _return_type = node->child->dataType;

    AST_NODE *param_list = node->child->rightSibling->rightSibling;
    if(param_list->child) {
        int offset = 16;
        FOR_SIBLINGS(param, param_list->child) {
            AST_NODE *id = param->child->rightSibling;
            ENTRY(id)->offset = offset;
            ENTRY(id)->is_pointer = IS_SCALAR(id) ? 0 : 1;
            offset += 8;
        }
    }
    genPrologue();

    _local_var_offset = 0;
    genBlockNode(param_list->rightSibling);
    _AR_offset += -_local_var_offset;

    genEpilogue();
}

void codeGeneration(AST_NODE *root) {
    output = fopen("output.s", "w");
    genProgramNode(root);
    fclose(output);
}

void genPrologue() {
    SWITCH_TO(Text);
    GEN_LABEL("_start_%s", _func_name);
    GEN_CODE("str x30, [sp, #0]");
    GEN_CODE("str x29, [sp, #-8]");
    GEN_CODE("add x29, sp, #-8");
    GEN_CODE("add sp, sp, #-16");
    GEN_CODE("ldr x30, =_frameSize_%s", _func_name);
    GEN_CODE("ldr w30, [x30, #0]");
    GEN_CODE("sub sp, sp, w30");

    int offset = 8;
    for(CALLEE_SAVED(i, int)) {
        GEN_CODE("str x%d, [sp, #%d]", i, offset);
        offset += 8;
    }
    for(CALLEE_SAVED(i, float)) {
        GEN_CODE("str d%d, [sp, #%d]", i, offset);
        offset += 8;
    }
    offset -= 8;

    _AR_offset = offset;
    GEN_LABEL("_begin_%s", _func_name);
}

void genEpilogue() {
    GEN_LABEL("_end_%s", _func_name);

    int offset = 8;
    for(CALLEE_SAVED(i, int)) {
        GEN_CODE("ldr x%d, [sp, #%d]", i, offset);
        offset += 8;
    }
    for(CALLEE_SAVED(i, float)) {
        GEN_CODE("ldr d%d, [sp, #%d]", i, offset);
        offset += 8;
    }

    GEN_CODE("ldr x30, [x29, #8]");
    GEN_CODE("add sp, x29, #8");
    GEN_CODE("ldr x29, [x29, #0]");
    GEN_CODE("ret x30");
    SWITCH_TO(Data);
    GEN_CODE("_frameSize_%s: .word %d\n", _func_name, _AR_offset);
    SWITCH_TO(Text);
}

int genSaveCallerRegs() {
    int count = 0;
    for(CALLER_SAVED(i, int)) {
        if(regs[i]) {
            GEN_CODE("str x%d, [sp, #%d]", i, -8 * count);
            ++count;
        }
    }
    for(CALLER_SAVED(i, float)) {
        if(FPregs[i]) {
            GEN_CODE("str d%d, [sp, #%d]", i, -8 * count);
            ++count;
        }
    }
    return count;
}

void genRestoreCallerRegs() {
    int count = 0;
    for(CALLER_SAVED(i, int)) {
        if(regs[i]) {
            GEN_CODE("ldr x%d, [sp, #%d]", i, -8 * count);
            ++count;
        }
    }
    for(CALLER_SAVED(i, float)) {
        if(FPregs[i]) {
            GEN_CODE("ldr d%d, [sp, #%d]", i, -8 * count);
            ++count;
        }
    }
}

int __evalPrameter(Parameter* param, AST_NODE* arg, int start_offset) {
    if(arg == NULL)
        return -start_offset;
    int offset = __evalPrameter(param->next, arg->rightSibling, start_offset);
    if(PARAM_IS_SCALAR(param)) {
        if(PARAM_SCALAR_TYPE(param) != arg->dataType) {
            if(arg->dataType == INT_TYPE) {
                GEN_SCVTF(arg, callee);
            } else {
                GEN_FCVTZS(arg, callee);
            }
        }
    }
    _param_info[_param_stack_top].type = arg->dataType;
    _param_info[_param_stack_top].place = arg->place;
    _param_info[_param_stack_top].offset = offset;
    _param_stack_top++;
    return offset - 8;
}

int evalParameter(Parameter* param, AST_NODE* relop) {
    _param_stack_top = 0;
    return -__evalPrameter(param, relop->child, 0);
}
