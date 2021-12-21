#include "codegen.hpp"

#include "ast/ast.hpp"
#include "errors/error.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace cpp {

Codegen::Codegen(std::string outputFilename, ast::AstNodePtr ast,std::string filename) {
    m_filename=filename;
    m_file.open(outputFilename);
    m_file << "#include <cstdio>\n#include <functional>\ntypedef enum{error___AssertionError,error___ZeroDivisionError} error;\n";
    m_env = createEnv();
    ast->accept(*this);
    m_file.close();
}

std::shared_ptr<SymbolTable<ast::AstNodePtr>>
Codegen::createEnv(std::shared_ptr<SymbolTable<ast::AstNodePtr>> parent) {
    return std::make_shared<SymbolTable<ast::AstNodePtr>>(parent);
}

// TODO: buffer it
std::string Codegen::write(std::string_view code) {
    if(save){
        res+=code;
    }
    else{
        m_file << code; 
    }
    return res;
}

std::string Codegen::mangleName(ast::AstNodePtr astNode) {
    return std::string("");
}

std::string Codegen::searchDefaultModule(std::string path,
                                         std::string moduleName) {
    for (auto& entry : std::filesystem::directory_iterator(path)) {
        if (std::filesystem::path(entry.path()).filename() == moduleName) {
            // TODO :ignore extensions?
            if (entry.is_regular_file()) {
                return entry.path();
            } else if (entry.is_directory()) {
                // TODO: avoid deeply nested folders
                searchDefaultModule(entry.path(), moduleName);
            }
        }
    }

    return "";
}

void Codegen::codegenFuncParams(std::vector<ast::parameter> parameters) {
    if (parameters.size()) {
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i)
                write(", ");

            parameters[i].p_type->accept(*this);
            write(" ");
            parameters[i].p_name->accept(*this);
        }
    }
}

bool Codegen::visit(const ast::Program& node) {
    for (auto& stmt : node.statements()) {
        stmt->accept(*this);
        write(";\n"); // TODO: will this break stuff later?
    }
    return true;
}

bool Codegen::visit(const ast::BlockStatement& node) {
    for (auto& stmt : node.statements()) {
        stmt->accept(*this);
        write(";\n");
    }
    return true;
}

bool Codegen::visit(const ast::ImportStatement& node) { return true; }

bool Codegen::visit(const ast::FunctionDefinition& node) {
    auto functionName =
        std::dynamic_pointer_cast<ast::IdentifierExpression>(node.name())
            ->value();
    if (not is_func_def){
        is_func_def=true;
        if (functionName == "main") {
            // we want the main function to always return 0 if success
            write("int main (");
            codegenFuncParams(node.parameters());
            write(") {\n");
            node.body()->accept(*this);
            write("return 0;\n}");
        } else {
            node.returnType()->accept(*this);
            write(" ");
            node.name()->accept(*this);
            write("(");
            codegenFuncParams(node.parameters());
            write(") {\n");
            node.body()->accept(*this);
            write("\n}");
        }
        is_func_def=false;
    }
    else{
        write("auto ");
        node.name()->accept(*this);
        write("=[=](");
        codegenFuncParams(node.parameters());
        write(")mutable->");
        node.returnType()->accept(*this);
        write(" {\n");
        node.body()->accept(*this);
        write("\n}");
    }
    return true;
}

bool Codegen::visit(const ast::VariableStatement& node) {
    if (node.varType()->type() != ast::KAstNoLiteral) {
        node.varType()->accept(*this);
        write(" ");
    }

    node.name()->accept(*this);

    if (node.value()->type() != ast::KAstNoLiteral) {
        write(" = ");
        node.value()->accept(*this);
    }
    return true;
}

bool Codegen::visit(const ast::ConstDeclaration& node) {
    write("const "); 
    if (node.constType()->type()!=ast::KAstNoLiteral){
        node.constType()->accept(*this);
    }
    write(" ");
    node.name()->accept(*this);
    write("=");
    node.value()->accept(*this);
    return true; 
    }

bool Codegen::visit(const ast::TypeDefinition& node) {
    write("typedef ");
    node.baseType()->accept(*this);
    write(" ");
    node.name()->accept(*this);
    return true;
}

bool Codegen::visit(const ast::PassStatement& node) {
    // we are making it a comment because ; is added
    // to each node at the end. we dont want that to
    // happen because it will result in ;; which is
    // an error
    write("\n//pass");
    return true;
}

bool Codegen::visit(const ast::IfStatement& node) {
    write("if (");
    node.condition()->accept(*this);
    write(") {\n");
    node.ifBody()->accept(*this);
    write("}");

    auto elifNode = node.elifs();
    if (elifNode.size() != 0) {
        write("\n");
        for (auto& body : elifNode) { // making sure that elif exists
            write("else if (");
            body.first->accept(*this);
            write(") {\n");
            body.second->accept(*this);
            write("}");
        }
    }

    auto elseNode = node.elseBody();
    if (elseNode->type() ==
        ast::KAstBlockStmt) { // making sure that else exists
        write("\nelse {\n");
        elseNode->accept(*this);
        write("}");
    }
    return true;
}

bool Codegen::visit(const ast::WhileStatement& node) {
    write("while (");
    node.condition()->accept(*this);
    write(") {\n");
    node.body()->accept(*this);
    write("}");
    return true;
}

bool Codegen::visit(const ast::ForStatement& node) { return true; }

bool Codegen::visit(const ast::MatchStatement& node) {
    auto toMatch = node.matchItem();
    auto cases = node.caseBody();
    auto defaultBody = node.defaultBody();
    write("\nwhile (true) {\n");
    for (size_t i = 0; i < cases.size(); ++i) {
        auto currCase = cases[i];
        if (currCase.first.size() == 1 &&
            currCase.first[0]->type() == ast::KAstNoLiteral) {
            if (i == 0) {
                currCase.second->accept(*this);
                write("\n");
            } else {
                write("else {\n");
                currCase.second->accept(*this);
                write("\n}\n");
            }
        } else if (i == 0) {
            write("if (");
            matchArg(toMatch, currCase.first);
            write(") {\n");
            currCase.second->accept(*this);
            write("\n}\n");
        } else {
            write("else if (");
            matchArg(toMatch, currCase.first);
            write(") {\n");
            currCase.second->accept(*this);
            write("\n}\n");
        }
    }

    if (defaultBody->type() != ast::KAstNoLiteral) {
        defaultBody->accept(*this);
    }
    write("\nbreak;\n}");
    return true;
}

bool Codegen::visit(const ast::ScopeStatement& node) {
    write("{\n");
    node.body()->accept(*this);
    write("\n}");
    return true;
}

bool Codegen::visit(const ast::CppStatement& node) {
    // we are making it a comment because ; is added to
    // each node at the end. we dont want that to happen
    // because it will result in ;; which is an error
    write(node.value() + "\n//");
    return true;
}

bool Codegen::visit(const ast::ReturnStatement& node) {
    write("return ");
    node.returnValue()->accept(*this);
    return true;
}

bool Codegen::visit(const ast::ContinueStatement& node) {
    write("continue");
    return true;
}

bool Codegen::visit(const ast::BreakStatement& node) {
    write("break");
    return true;
}

bool Codegen::visit(const ast::DecoratorStatement& node) {
    auto items = node.decoratorItem();
    auto body = node.body();
    std::string contains;
    std::string x;
    std::string prev;
    save=true;
    if (res!=""){
        prev=res;
        res="";
    }
    if(body->type()==ast::KAstFunctionDef || body->type()==ast::KAstStatic){
        std::shared_ptr<ast::FunctionDefinition> function;
        if (body->type()==ast::KAstStatic){
            write("static ");
            function = std::dynamic_pointer_cast<ast::FunctionDefinition>(
                        std::dynamic_pointer_cast<ast::StaticStatement>(body)->body()
                        );
        }
        else{
            function = std::dynamic_pointer_cast<ast::FunctionDefinition>(body);
        }
        write("auto ");
        function->name()->accept(*this);
        write("=");
        x+=res;
        res="";
        if(is_func_def){
            write("[=](");
        }
        else{
            write("[](");
        }
        codegenFuncParams(function->parameters());
        write(")mutable->");
        function->returnType()->accept(*this);
        write("{\n");
        if(not is_func_def){
            is_func_def=true;
            function->body()->accept(*this);
            is_func_def=false;
        }
        else{
            function->body()->accept(*this);
        }
        write("\n}");
        contains=res;
        res="";
    }
    for (size_t i = items.size() - 1; i != (size_t)-1; i--){
        ast::AstNodePtr item=items[i];
        contains=wrap(item,contains);
    }
    if (prev==""){
        save=false;
        write(x+contains);
    }
    else{
        write(prev+x+contains);
    }
    return true;
}

bool Codegen::visit(const ast::ListLiteral& node) { return true; }

bool Codegen::visit(const ast::DictLiteral& node) { return true; }

bool Codegen::visit(const ast::ListOrDictAccess& node) { return true; }

bool Codegen::visit(const ast::BinaryOperation& node) {
    if (node.op().keyword == "**") {
        write("_PEREGRINE_POWER(");
        node.left()->accept(*this);
        write(",");
        node.right()->accept(*this);
        write(")");
    } else if (node.op().keyword == "//") {
        write("_PEREGRINE_FLOOR(");
        node.left()->accept(*this);
        write("/");
        node.right()->accept(*this);
        write(")");
    } else {
        write("(");
        node.left()->accept(*this);
        write(" " + node.op().keyword + " ");
        node.right()->accept(*this);
        write(")");
    }
    return true;
}

bool Codegen::visit(const ast::PrefixExpression& node) {
    write("(" + node.prefix().keyword + " ");
    node.right()->accept(*this);
    write(")");
    return true;
}

bool Codegen::visit(const ast::FunctionCall& node) {
    node.name()->accept(*this);
    write("(");

    auto args = node.arguments();
    if (args.size()) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i)
                write(", ");
            args[i]->accept(*this);
        }
    }

    write(")");
    return true;
}

bool Codegen::visit(const ast::DotExpression& node) { 
    //FIXME: Not very elegent
    if (not is_dot_exp){
        is_dot_exp=true;
        if (node.owner()->type()==ast::KAstIdentifier){
            std::string name = std::dynamic_pointer_cast<ast::IdentifierExpression>(node.owner())->value();
            if(std::count(enum_name.begin(), enum_name.end(), name)){
                write(name+"___");
                node.referenced()->accept(*this); 
            }
            else{
                node.owner()->accept(*this);
                write(".");
                node.referenced()->accept(*this);   
            }
        is_dot_exp=false;
        }
        else {
            node.owner()->accept(*this);
            write(".");
            node.referenced()->accept(*this); 
        }
        is_dot_exp=false;
    }
    else{
        node.owner()->accept(*this);
        write(".");
        node.referenced()->accept(*this);
    }  
    return true; 
}

bool Codegen::visit(const ast::IdentifierExpression& node) {
    if (is_enum){
        write(enum_name.back()+"___");
    }
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::TypeExpression& node) {
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::ListTypeExpr& node) { return true; }

bool Codegen::visit(const ast::DictTypeExpr& node) { return true; }

bool Codegen::visit(const ast::FunctionTypeExpr& node) {
    write("std::function<");
    if (node.returnTypes().size() == 0) {
        write("void (");
    } else {
        node.returnTypes()[0]->accept(*this);
        write("(");
    }
    auto argTypes = node.argTypes();
    if (argTypes.size() > 0) {
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (i)
                write(",");
            argTypes[i]->accept(*this);
        }
    }
    write(")>");
    return true;
}

bool Codegen::visit(const ast::NoLiteral& node) {
    // nothing ig
    return true;
}

bool Codegen::visit(const ast::IntegerLiteral& node) {
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::DecimalLiteral& node) {
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::StringLiteral& node) {
    write("\"" + node.value() + "\"");
    return true;
}

bool Codegen::visit(const ast::BoolLiteral& node) {
    write((node.value() == "True") ? "true" : "false");
    return true;
}

bool Codegen::visit(const ast::NoneLiteral& node) {
    write("NULL");
    return true;
}
bool Codegen::visit(const ast::AssertStatement& node){
    write("if(! ");
    node.condition()->accept(*this);
    write("){\n");
    write("printf(\"AssertionError : in line "+std::to_string(node.token().line)+" in file "+m_filename+"\\n   "+node.token().statement+"\\n\");fflush(stdout);throw error___AssertionError;");
    write("\n}");
    return true;
}
bool Codegen::visit(const ast::StaticStatement& node){
    write("static ");
    node.body()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::InlineStatement& node){
    write("inline ");
    node.body()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::RaiseStatement& node){
    write("throw ");
    node.value()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::UnionLiteral& node){
    write("typedef union{\n");
    for (auto& element:node.elements()){
        element.first->accept(*this);
        write(" ");
        element.second->accept(*this);
        write(";\n");
    }
    write("\n}");
    node.name()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::EnumLiteral& node){
    write("typedef enum{\n");
    auto fields=node.fields();
    std::string name=std::dynamic_pointer_cast<ast::IdentifierExpression>(node.name())->value();
    enum_name.push_back(name);
    for (size_t i=0;i<fields.size();++i){
        auto field=fields[i];
        write(name+"___");
        field.first->accept(*this);
        is_enum=true;
        if (field.second->type()!=ast::KAstNoLiteral){
            write(" = ");
            field.second->accept(*this);
        }
        is_enum=false;
        if (i!=fields.size()-1){
            write(",\n");
        }
    }
    write("\n}");
    write(name);
    return true;
}
} // namespace cpp
