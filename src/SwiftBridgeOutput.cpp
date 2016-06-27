//
//  SwiftppObjcOutput.cpp
//  swiftpp
//
//  Created by Sandy Martel on 2014/09/10.
//  Copyright (c) 2014年 Sandy Martel. All rights reserved.
//

#include "SwiftBridgeOutput.h"
#include <clang/Frontend/CompilerInstance.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include "CodeTemplate.h"
#include "SwiftBridgeTmpl.h"

#include <iostream>

void SwiftppObjcOutput::write_impl()
{
	// select an output folder
	auto outputFolder = _data->outputFolder();
	if ( outputFolder.empty() )
	{
		outputFolder = _inputFile;
		auto pos = outputFolder.rfind( '.' );
		if ( pos != std::string::npos )
		{
			outputFolder = outputFolder.substr( 0, pos );
			outputFolder += "/";
		}
		else
			outputFolder += "-cxx-bridge/";
	}
	
	CodeTemplateModel model;
	buildCodeModel( model );
	
	auto ostr = _ci->createOutputFile( outputFolder + "c_impl.h", false, true, "", "", true, true );
	if ( ostr )
		write_cxx_bridge_h( model, *ostr );
	
	auto cpp_ext = "cpp";
	if ( _data->anyObjcTypes() )
		cpp_ext = "mm";
	ostr = _ci->createOutputFile( outputFolder + "c_impl." + cpp_ext, false, true, "", "", true, true );
	if ( ostr )
		write_cxx_bridge_cpp( model, *ostr );
	
	ostr = _ci->createOutputFile( outputFolder + "bridge.swift", false, true, "", "", true, true );
	if ( ostr )
		write_cxx_bridge_swift( model, *ostr );
}

void SwiftppObjcOutput::buildCodeModel( CodeTemplateModel &model )
{
	auto data = _data;
	
	model.names["objc_forward_decl"] = [data]( llvm::raw_ostream &ostr )
		{
			auto l = data->allObjcTypes();
			for ( auto &it : l )
			{
				ostr << "@class " << it << ";\n";
			}
		};
	model.names["bridge_include"] = data->formatIncludeName( _inputFile );
	model.names["ns"] = NS_PREFIX;
	model.names["std_string_size"] = std::to_string( sizeof( void * ) * 3 );
	
	model.sections["frameworks"] = CodeTemplateModel::ListSection{ data->objCFrameworks().size(),
					[data]( size_t i, CodeTemplateModel &o_model )
					{
						auto framework_name = data->objCFrameworks()[i];
						o_model.names["framework_name"] = framework_name;
					}
				};
	
	model.sections["has_enums"] = CodeTemplateModel::BoolSection( not data->enums().empty() );
	model.sections["enums"] = CodeTemplateModel::ListSection{ data->enums().size(),
			[data]( size_t i, CodeTemplateModel &o_model )
			{
				auto e = &(data->enums()[i]);
				o_model.names["enum_name"] = e->name();
				o_model.sections["enum_values"] = CodeTemplateModel::ListSection{ e->values().size(),
						[e]( size_t i, CodeTemplateModel &o_model )
						{
							auto &v = e->values()[i];
							o_model.names["one_name"] = v.first;
							o_model.names["one_value"] = std::to_string( v.second );
						}
					};
			}
		};

	auto classes = [this, data]( size_t i, CodeTemplateModel &o_model )
		{
			auto classPtr = &(data->classes()[i]);

			o_model.names["class_name"] = classPtr->name();

			// bases
			o_model.sections["base_classes"] = CodeTemplateModel::ListSection{ classPtr->bases().size(),
					[this,classPtr]( size_t i, CodeTemplateModel &o_model )
					{
						auto base_name = classPtr->bases()[i];
						o_model.names["base_name"] = base_name;
					}
				};

			// constructors
			o_model.sections["constructors"] = CodeTemplateModel::ListSection{ classPtr->constructors().size(),
					[this,classPtr]( size_t i, CodeTemplateModel &o_model )
					{
						auto constructor = classPtr->constructors()[i];
						// params
						o_model.sections["params"] = CodeTemplateModel::ListSection{ constructor->params().size(),
							[this,constructor]( size_t i, CodeTemplateModel &o_model )
							{
								auto param = &(constructor->params()[i]);
								o_model.names["param_name"] = param->name();
								o_model.names["param_c_type"] = this->param_c_type( param->type() );
								o_model.names["param_cxx_type"] = this->param_cxx_type( param->type() );
								o_model.names["param_clean_name"] = param->cleanName();
								o_model.names["param_swift_type"] = this->param_swift_type( param->type() );
							}
						};
					}
				};

			// virtual methods
			o_model.sections["virtual_methods"] = CodeTemplateModel::ListSection{ classPtr->virtualMethods().size(),
				[this,classPtr]( size_t i, CodeTemplateModel &o_model )
				{
					auto method = classPtr->virtualMethods()[i];
						o_model.names["name"] = method->name();
						o_model.names["return_c_type"] = this->return_c_type( method->returnType() );
						o_model.names["return_cxx_type"] = this->return_cxx_type( method->returnType() );
						o_model.names["return_converter_c_to_cxx"] = this->returnConverterForCType2CXXType( method->returnType() );
						o_model.names["return_converter_c_to_swift"] = this->returnConverterForCType2SwiftType( method->returnType() );
						if ( not method->returnType()->isVoidType() )
						{
							o_model.sections["has_return_value"] = CodeTemplateModel::BoolSection( true );
							o_model.names["return_swift_c_type"] = this->return_swift_c_type( method->returnType() );
						}
						else
							o_model.sections["has_return_value"] = CodeTemplateModel::BoolSection( false );
						o_model.names["return_converter_swift_to_c"] = this->return_converter_swift_to_c( method->returnType() );
						o_model.sections["params"] = CodeTemplateModel::ListSection{ method->params().size(),
							[this,method]( size_t i, CodeTemplateModel &o_model )
							{
								auto param = &(method->params()[i]);
								o_model.names["param_name"] = param->name();
								o_model.names["param_c_type"] = this->param_c_type( param->type() );
								o_model.names["param_cxx_type"] = this->param_cxx_type( param->type() );
								o_model.names["param_as_c_type"] = this->param_as_c_type( *param );
								o_model.names["param_swift_c_type"] = this->param_swift_c_type( param->type() );
								o_model.names["param_clean_name"] = param->cleanName();
								o_model.names["param_as_swift_type"] = [this,i,param]( llvm::raw_ostream &ostr )
									{
										ostr << param->cleanName() << ":";
										ostr << this->converterForCType2SwiftType( param->type(), param->cleanName() );
									};
							}
						};
				}
			};
			
			// methods
			o_model.sections["methods"] = CodeTemplateModel::ListSection{ classPtr->methods().size(),
					[this,classPtr]( size_t i, CodeTemplateModel &o_model )
					{
						auto method = classPtr->methods()[i];
						o_model.names["name"] = method->name();
						o_model.sections["is_static"] = CodeTemplateModel::BoolSection( method->isStatic() );
						o_model.sections["is_non_static"] = CodeTemplateModel::BoolSection( not method->isStatic() );
						o_model.sections["is_protected"] = CodeTemplateModel::BoolSection( method->access() == CXXMethod::access_t::kProtected );
						o_model.names["return_c_type"] = this->return_c_type( method->returnType() );
						o_model.names["return_cxx_type"] = this->return_cxx_type( method->returnType() );
						if ( not method->returnType()->isVoidType() )
						{
							o_model.sections["has_return_value"] = CodeTemplateModel::BoolSection( true );
							o_model.names["return_swift_type"] = this->return_swift_type( method->returnType() );
							o_model.names["return_converter_cxx_to_c"] = this->returnConverterForCXXType2CType( method->returnType() );
							o_model.names["return_converter_c_to_swift"] = this->returnConverterForCType2SwiftType( method->returnType() );
						}
						else
							o_model.sections["has_return_value"] = CodeTemplateModel::BoolSection( false );
						o_model.sections["params"] = CodeTemplateModel::ListSection{ method->params().size(),
							[this,method]( size_t i, CodeTemplateModel &o_model )
							{
								auto param = &(method->params()[i]);
								o_model.names["param_name"] = param->name();
								o_model.names["param_c_type"] = this->param_c_type( param->type() );
								o_model.names["param_cxx_type"] = this->param_cxx_type( param->type() );
								o_model.names["param_as_cxx_type"] = this->param_as_cxx_type( *param );
								o_model.names["param_clean_name"] = param->cleanName();
								o_model.names["param_swift_type"] = this->param_swift_type( param->type() );
							}
						};
					}
				};
		};
	
	model.sections["classes"] = CodeTemplateModel::ListSection{ data->classes().size(), classes };
}

std::string SwiftppObjcOutput::param_swift_type( const clang::QualType &i_cxxtype ) const
{
	return type2SwiftTypeString( i_cxxtype );
}

std::string SwiftppObjcOutput::return_swift_type( const clang::QualType &i_cxxtype ) const
{
	return type2SwiftTypeString( i_cxxtype );
}

std::string SwiftppObjcOutput::type2SwiftTypeString( const clang::QualType &i_cxxtype ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	// is there a converter?
	for ( auto it : _data->converters() )
	{
		if ( cxxtype == type2UndecoratedCXXTypeString( it.from() ) )
		{
			// converter found, use the converted type
			if ( clang::isa<clang::ObjCObjectPointerType>( it.to() ) )
				return clang::QualType::getAsString(it.to().getTypePtr()->getPointeeType().split());

			return type2UndecoratedCXXTypeString( it.to() );
		}
	}
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return "String";
	
	if ( isCXXVectorType( i_cxxtype ) or isCXXListType( i_cxxtype ) )
	{
		assert( false );
	}
	if ( isCXXMapType( i_cxxtype ) or isCXXUnorderedMapType( i_cxxtype ) )
	{
		assert( false );
	}
	if ( isCXXSetType( i_cxxtype ) )
	{
		assert( false );
	}
	if ( cxxtype == "bool" )
		return "CBool";
	if ( cxxtype == "char" or cxxtype == "signed char" )
		return "CChar";
	if ( cxxtype == "unsigned char" )
		return "CUnsignedChar";
	if ( cxxtype == "short" )
		return "CShort";
	if ( cxxtype == "unsigned short" )
		return "CUnsignedShort";
	if ( cxxtype == "int" )
		return "CInt";
	if ( cxxtype == "unsigned int" )
		return "CUnsignedInt";
	if ( cxxtype == "long" )
		return "CLong";
	if ( cxxtype == "unsigned long" )
		return "CUnsignedLong";
	if ( cxxtype == "long long" )
		return "CLongLong";
	if ( cxxtype == "unsigned long long" )
		return "CUnsignedLongLong";
	if ( cxxtype == "wchar_t" )
		return "CWideChar";
	if ( cxxtype == "char16_t" )
		return "CChar16";
	if ( cxxtype == "char32_t" )
		return "CChar32";
	if ( cxxtype == "float" )
		return "CFloat";
	if ( cxxtype == "double" )
		return "CDouble";
	if ( strncmp( cxxtype.c_str(), "enum ", 5 ) == 0 )
		return cxxtype.substr( 5 );
	
	//! @todo: warn for unsupported types
	assert( false );
	
	return cxxtype;
}

std::string SwiftppObjcOutput::param_swift_c_type( const clang::QualType &i_cxxtype ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	// is there a converter?
	for ( auto it : _data->converters() )
	{
		if ( cxxtype == type2UndecoratedCXXTypeString( it.from() ) )
		{
			// converter found, use the converted type
			if ( clang::isa<clang::ObjCObjectPointerType>( it.to() ) )
				return clang::QualType::getAsString(it.to().getTypePtr()->getPointeeType().split()) + "!";

			return type2UndecoratedCXXTypeString( it.to() );
		}
	}
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return "UnsafePointer<CChar>?";
	
	if ( isCXXVectorType( i_cxxtype ) or isCXXListType( i_cxxtype ) )
	{
		assert( false );
	}
	if ( isCXXMapType( i_cxxtype ) or isCXXUnorderedMapType( i_cxxtype ) )
	{
		assert( false );
	}
	if ( isCXXSetType( i_cxxtype ) )
	{
		assert( false );
	}
	if ( cxxtype == "int" )
		return "CInt";
	
	//! @todo: warn for unsupported types
	assert( false );
	
	return cxxtype;
}

void SwiftppObjcOutput::write_cxx_bridge_h( CodeTemplateModel &i_model, llvm::raw_ostream &ostr ) const
{
	CodeTemplate renderer( kCXX_BRIDGE_H_TEMPLATE );
	renderer.render( i_model, ostr );
}

void SwiftppObjcOutput::write_cxx_bridge_cpp( CodeTemplateModel &i_model, llvm::raw_ostream &ostr ) const
{
	CodeTemplate renderer( kCXX_BRIDGE_CPP_TEMPLATE );
	renderer.render( i_model, ostr );
}

void SwiftppObjcOutput::write_cxx_bridge_swift( CodeTemplateModel &i_model, llvm::raw_ostream &ostr ) const
{
	CodeTemplate renderer( kCXX_BRIDGE_SWIFT_TEMPLATE );
	renderer.render( i_model, ostr );
}

std::string SwiftppObjcOutput::param_as_cxx_type( const CXXParam &i_param ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_param.type() ) );
	
	// is there a converter?
	for ( auto converter : _data->converters() )
	{
		if ( cxxtype == type2UndecoratedCXXTypeString( converter.to() ) )
		{
			// converter found, use the converted type
			return std::string("swift_converter::") + converter.name() + "(" + i_param.name() + ")";
		}
	}
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return std::string("std::string(") + i_param.name() + ")";
	
	clang::QualType valueType;
	if ( isCXXVectorType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXListType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXMapType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXUnorderedMapType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXSetType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	
	return i_param.name();
}
std::string SwiftppObjcOutput::returnConverterForCType2CXXType( const clang::QualType &i_cxxtype ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	if ( cxxtype == "std::string" )
		return NS_PREFIX "StringWrapper2String";
	
	clang::QualType valueType;
	if ( isCXXVectorType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXListType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXUnorderedMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXSetType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	
	return std::string();
}

std::string SwiftppObjcOutput::returnConverterForCXXType2CType( const clang::QualType &i_cxxtype ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return NS_PREFIX "StringWrapper_create";
	
	clang::QualType valueType;
	if ( isCXXVectorType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXListType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXUnorderedMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXSetType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}

	return std::string();
}

std::string SwiftppObjcOutput::returnConverterForCType2SwiftType( const clang::QualType &i_cxxtype ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return "String";
	
	clang::QualType valueType;
	if ( isCXXVectorType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXListType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXUnorderedMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXSetType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}

	return std::string();
}

std::string SwiftppObjcOutput::param_as_c_type( const CXXParam &i_param ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_param.type() ) );
	
	// is there a converter?
	for ( auto converter : _data->converters() )
	{
		if ( cxxtype == type2UndecoratedCXXTypeString( converter.from() ) )
		{
			// converter found, use the converted type
			return std::string("swift_converter::") + converter.name() + "(" + i_param.name() + ")";
		}
	}
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return i_param.name() + ".c_str()";
	
	clang::QualType valueType;
	if ( isCXXVectorType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXListType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXMapType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXUnorderedMapType( i_param.type(), &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXSetType( i_param.type(), &valueType ) )
	{
		assert( false );
	}

	return i_param.name();
}

std::string SwiftppObjcOutput::converterForCType2SwiftType( const clang::QualType &i_cxxtype, const std::string &i_code ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return "String(cString:" + i_code + "!)";
	
	clang::QualType valueType;
	if ( isCXXVectorType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXListType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXUnorderedMapType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}
	else if ( isCXXSetType( i_cxxtype, &valueType ) )
	{
		assert( false );
	}

	return i_code;
}

std::string SwiftppObjcOutput::return_swift_c_type( const clang::QualType &i_cxxtype ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return NS_PREFIX "StringWrapper";
	
	if ( cxxtype == "int" )
		return "CInt";
	
	//! @todo: warn for unsupported types
	assert( false );
	
	return cxxtype;
}

std::string SwiftppObjcOutput::return_converter_swift_to_c( const clang::QualType &i_cxxtype ) const
{
	std::string cxxtype( type2UndecoratedCXXTypeString( i_cxxtype ) );
	
	// add a few default converters
	if ( cxxtype == "std::string" )
		return NS_PREFIX "StringWrapper_create";
	
	return cxxtype;
}
