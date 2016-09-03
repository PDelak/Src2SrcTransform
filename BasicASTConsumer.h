#ifndef RECURSIVE_AST_VISITOR_H
#define RECURSIVE_AST_VISITOR_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ParentMap.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Index/DeclReferenceMap.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/ASTConsumers.h"
#include "clang/Rewrite/Rewriter.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/DenseSet.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include <boost/tuple/tuple.hpp>
#include <iostream>
#include <sstream>
using namespace clang;
using namespace std;
class BasicASTConsumer : public ASTConsumer,
                         public RecursiveASTVisitor<BasicASTConsumer> {
  
  Rewriter Rewrite;
  Diagnostic &Diags;
  const LangOptions &LangOpts;
  unsigned RewriteFailedDiag;

  ASTContext *Context;
  SourceManager *SM;
  TranslationUnitDecl *TUDecl;
  FileID MainFileID;
  const char *MainFileStart, *MainFileEnd;

  std::string InFileName;
  llvm::raw_ostream* OutFile;

  public:
   BasicASTConsumer(std::string& inFile, llvm::raw_ostream *OS,
        Diagnostic &D, const LangOptions &LOpts)
      : Diags(D)
      , LangOpts(LOpts)
      , InFileName(inFile)
      , OutFile(OS)
  {}

  virtual void Initialize(ASTContext &context) 
  {
    Context = &context;
    SM = &Context->getSourceManager();
    TUDecl = Context->getTranslationUnitDecl();

    // Get the ID and start/end of the main file.
    MainFileID = SM->getMainFileID();
    const llvm::MemoryBuffer *MainBuf = SM->getBuffer(MainFileID);
    MainFileStart = MainBuf->getBufferStart();
    MainFileEnd = MainBuf->getBufferEnd();

    Rewrite.setSourceMgr(Context->getSourceManager(), Context->getLangOptions());

  }
    /// HandleTranslationUnit - This method is called when the ASTs for entire
    /// translation unit have been parsed.
    virtual void HandleTranslationUnit(ASTContext &Ctx)
    {
      std::string preamble = "#include\"tools.h\"\n\n";
      Rewrite.InsertText(SM->getLocForStartOfFile(MainFileID), preamble, false);

      TraverseDecl(Ctx.getTranslationUnitDecl());

      // Get the buffer corresponding to MainFileID.
      // If we haven't changed it, then we are done.
      if (const RewriteBuffer *RewriteBuf =
        Rewrite.getRewriteBufferFor(MainFileID)) {
        llvm::outs() << "Src file changed.\n";
        *OutFile << std::string(RewriteBuf->begin(), RewriteBuf->end());
      } else {
        llvm::errs() << "No changes.\n";
      }

      OutFile->flush();
    }

    bool VisitCXXConstructorDecl(CXXConstructorDecl* constructorDecl) {
      
      for(int i = 0; i < constructorDecl->getNumParams() ; i++) {
        std::stringstream ss;  
        ParmVarDecl* param = constructorDecl->getParamDecl(i);
        serializeParam(param,ss);
        SourceRange range = param->getSourceRange();
        Rewrite.ReplaceText(param->getLocStart(),Rewrite.getRangeSize(range),ss.str());
      }  
      return true;
    }

    bool VisitCXXRecordDecl(CXXRecordDecl *D) {
      return true;
    }

    bool VisitCXXDeleteExpr(CXXDeleteExpr* deleteExpr) {
      Expr* arg = static_cast<Expr*>(deleteExpr->getArgument());
      std::string argName;
      SourceRange range = deleteExpr->getSourceRange();
      QualType PointeeTy;        
      if(ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(arg)) 
      {
        Expr* refExpr = ICE->getSubExpr();
        boost::tie(argName,PointeeTy) = nameAndType(refExpr);
      } 
      else 
        if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(arg)) 
          boost::tie(argName,PointeeTy) = nameAndType(memberExpr);
      
      std::stringstream ss;
      serializeDeleterExpression(PointeeTy,argName,ss);

      Rewrite.ReplaceText(deleteExpr->getLocStart(),
        Rewrite.getRangeSize(range),ss.str());    
      return true;
    }
    bool VisitCXXNewExpr(CXXNewExpr* newExpr) {
      checkNewExprArraySize(newExpr);
      QualType Type = newExpr->getType();
      QualType PointeeTy = Type->getPointeeType();
      std::stringstream ss;
      serializePointerType(PointeeTy,ss);
      serializeNewExpression(PointeeTy,ss);
      SourceRange range = newExpr->getSourceRange();

      Rewrite.ReplaceText(newExpr->getLocStart(),
                          Rewrite.getRangeSize(range),
                          ss.str());    
      return true;
    }
    bool VisitFunctionDecl(FunctionDecl* D) {
      
      /// Do not visit CXXConstructor or CXXDestrutor      
      if (D->getKind() == Decl::CXXDestructor || 
          D->getKind() == Decl::CXXConstructor)
          return true;
      
      /// Currently the whole function header
      /// is rewritten due to problem of getting 
      /// result sourcelocation        
      QualType resultType = D->getResultType();
      QualType PointeeTy = resultType->getPointeeType();
      stringstream ss;      
      if(!PointeeTy.isNull())  
        serializePointerType(PointeeTy,ss);            
      else 
        ss << resultType.getAsString()  << " ";                         
      
      ss << " " << D->getNameAsString() << "(";
      
      for(int i = 0; i < D->getNumParams() ; i++) {
        ParmVarDecl* param = D->getParamDecl(i);
        serializeParam(param,ss);
        if (i < D->getNumParams() - 1 ) ss << ",";        
      }      
      ss << ")";

      SourceRange range 
        = D->getTypeSourceInfo()->getTypeLoc().getSourceRange();

      Rewrite.ReplaceText(D->getTypeSpecStartLoc(),
            Rewrite.getRangeSize(range),ss.str() );
      return true;     
    }
    bool VisitCXXMethodDecl(CXXMethodDecl* D) {
      return true;
    }
    bool VisitVarDecl(VarDecl* D) {
      bool initialized = false;
      std::string initializationExpr;
      
      SourceRange range = D->getTypeSourceInfo()->getTypeLoc().getSourceRange();
      Expr * initExpr = D->getInit();
    
      APInt intValue;
      QualType Type = D->getType();
      QualType PointeeTy = Type->getPointeeType();

      if (!PointeeTy.isNull() && D->getKind() == Decl::Kind::Var) {
        std::stringstream ss;
        serializePointerType(PointeeTy,ss); 
        Rewrite.ReplaceText(D->getLocStart(),
            Rewrite.getRangeSize(range) ,ss.str());

        if (!initExpr) return true;
        ss.swap(std::stringstream());
        serializeInitializationExpression(initExpr,ss);      

        if (ss.str().empty()) return true;

        std::stringstream exprSS;
        serializePointerType(PointeeTy,exprSS);
        exprSS << "(" << ss.str() << ")";
        Rewrite.ReplaceText(initExpr->getLocStart(),
          Rewrite.getRangeSize(initExpr->getSourceRange()) ,exprSS.str());
        
      }
      return true;
    }
    bool VisitReturnStmt(ReturnStmt* returnStmt) {
      stringstream ss;
      Expr* returnEx = returnStmt->getRetValue();
      SourceRange range = returnEx->getSourceRange();
      if(DeclRefExpr* returnExpr 
        = dyn_cast<DeclRefExpr>(returnEx)) 
      {        
        QualType Type = returnExpr->getType();
        QualType PointeeTy = Type->getPointeeType();
        if (PointeeTy.isNull()) 
          return true;
        serializePointerType(PointeeTy,ss);        
      } 
      else if (ImplicitCastExpr *ICE 
        = dyn_cast<ImplicitCastExpr>(returnEx)) 
      {       
        SourceRange range = ICE->getSourceRange();
        QualType Type = ICE->getSubExpr()->getType();
        if (isa<DeclRefExpr>(ICE->getSubExpr()))
          serializePointerType(Type->getPointeeType(),ss);
        else
          serializePointerType(Type,ss);      
      }
      ss << "(" ;
      serializeInitializationExpression(returnEx,ss);
      ss  << ")";
      Rewrite.ReplaceText(returnEx->getLocStart(),
                          Rewrite.getRangeSize(range),
                          ss.str());
    
      return true;
    }
    bool VisitBinaryOperator(BinaryOperator* op)
    {
      if (op->isAssignmentOp())
        return handleAssignmentOperator(op);
      else if(op->isAdditiveOp()) 
        return handleAdditiveOperator(op);  
      
      return true;
    }

    bool VisitFieldDecl(FieldDecl* D) {
      stringstream ss;
      SourceRange range = D->getTypeSourceInfo()->getTypeLoc().getSourceRange();
      QualType Type = D->getType();
      QualType PointeeTy = Type->getPointeeType();
      /// if this is not a pointer return
      if (PointeeTy.isNull()) return true;    
      /// Handler invocation
      handleFieldTypeDecl(Type,D->getTypeSourceInfo()->getTypeLoc());
      
      return true;
    }
private:
    void serializePointerType(QualType type,std::stringstream& ss)
    {
      ss << "boost::shared_ptr<" << type.getAsString() << ">";
    }
    void serializeDeleterExpression(QualType type, const std::string argName, std::stringstream& ss)
    {
      ss << "util::deleter<" << type.getAsString() << ">"
         << "::delete_ptr(" << argName << ")";
    }
    void serializeNewExpression(QualType type,std::stringstream& ss)
    {
      ss << "(new " << type.getAsString() << ",";
      ss << "util::deleter<" << type.getAsString() << ">())";
    }
    
    void serializeParam(ParmVarDecl* param,std::stringstream& ss)
    {
      QualType Type = param->getType();
      QualType PointeeTy = Type->getPointeeType();
      if(PointeeTy.isNull()) ss << Type.getAsString() ;
      else serializePointerType(PointeeTy,ss);
      ss << " " << param->getNameAsString();
    }
    void serializeInitializationExpression(Expr* expr,std::stringstream& ss)
    {      
      if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(expr)) {
          if(IntegerLiteral *iliteral = dyn_cast<IntegerLiteral>(ICE->getSubExpr())) 
          {
            APInt intValue;
            iliteral = static_cast<IntegerLiteral*>(ICE->getSubExpr());
            intValue = iliteral->getValue();
            std::string type = iliteral->getType().getAsString();
            ss << "static_cast<" << type << ">";
            ss << "(" << intValue.toString(10U,true) << "),";
            ss << "util::deleter<" << iliteral->getType().getAsString() << ">()";
          } 
          else if(DeclRefExpr* refExpr = dyn_cast<DeclRefExpr>(ICE->getSubExpr())) 
          {
            ss << refExpr->getNameInfo().getAsString();
          }
      }
      else if(DeclRefExpr* refExpr = dyn_cast<DeclRefExpr>(expr)) 
        ss << refExpr->getNameInfo().getAsString();
    }
    std::pair<std::string,QualType> nameAndType(Expr* expr)
    {
       std::pair<std::string,QualType> result;
       if(DeclRefExpr *declRefExpr = dyn_cast<DeclRefExpr>(expr)) {        
        result = std::make_pair(declRefExpr->getNameInfo().getAsString(),
                                declRefExpr->getType()->getPointeeType());
      } else if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(expr)) {
          result = std::make_pair(memberExpr->getMemberNameInfo().getAsString(),        
                                  memberExpr->getType()->getPointeeType());
      }
       return result;
    }
    void checkNewExprArraySize(CXXNewExpr* newExpr)
    {
      std::cout << newExpr->isArray() << std::endl;
      
      /// get new array size if any       
      Expr* expr = newExpr->getArraySize();
      if (expr)
        if(ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(expr)) {
        Expr* refExpr = ICE->getSubExpr();
          if (IntegerLiteral *iv = dyn_cast<IntegerLiteral>(refExpr)){
            std::cout << iv->getValue().toString(10,true) << std::endl;
          }
        }
    }
    bool handleAssignmentOperator(BinaryOperator* op)
    {
      if(isa<DeclRefExpr>(op->getLHS()) && isa<DeclRefExpr>(op->getRHS())) 
      {
        DeclRefExpr * lhsExpr = static_cast<DeclRefExpr*>(op->getLHS());
        DeclRefExpr * rhsExpr = static_cast<DeclRefExpr*>(op->getRHS());
      } 
      else if (dyn_cast<ImplicitCastExpr>(op->getRHS()) && isa<DeclRefExpr>(op->getLHS())) 
      {
        DeclRefExpr * lhsExpr = static_cast<DeclRefExpr*>(op->getLHS());
        if(ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(op->getRHS())) {
          if(IntegerLiteral *iliteral = dyn_cast<IntegerLiteral>(ICE->getSubExpr())) {
            APInt intValue;
            iliteral = static_cast<IntegerLiteral*>(ICE->getSubExpr());
            intValue = iliteral->getValue();
            if(intValue != 0) return true;
            SourceRange range = op->getSourceRange();
            std::string resetStr = lhsExpr->getNameInfo().getAsString();
            resetStr += ".reset()";
            Rewrite.ReplaceText(op->getLocStart(),
                Rewrite.getRangeSize(range),
                resetStr);    
        
          }
        }
      } 
      return true;
    }
    bool handleAdditiveOperator(BinaryOperator* op)
    {
      std::cout << "add op" << std::endl;
      return true;
    }
    
    /// beginning of handler interface
    void handleFieldTypeDecl(QualType type, TypeLoc location)
    {
      std::stringstream ss;      
      serializePointerType(type->getPointeeType(),ss); 
      
      SourceRange range = location.getSourceRange();
      Rewrite.ReplaceText(
              location.getBeginLoc(),
              Rewrite.getRangeSize(range),ss.str());

    }

};

#endif
