/*---------------------------------------------------------------------\
|                                                                      |
|                      __   __    ____ _____ ____                      |
|                      \ \ / /_ _/ ___|_   _|___ \                     |
|                       \ V / _` \___ \ | |   __) |                    |
|                        | | (_| |___) || |  / __/                     |
|                        |_|\__,_|____/ |_| |_____|                    |
|                                                                      |
|                               core system                            |
|                                                        (C) SuSE GmbH |
\----------------------------------------------------------------------/

   File:       YCPInterpreter.cc

   Author:     Mathias Kettner <kettner@suse.de>
   Maintainer: Klaus Kaempf <kkaempf@suse.de>

/-*/
/*
 * YCP interpreter that defines the builtins
 */


#include "y2log.h"
#include "YCPInterpreter.h"
#include "ycpless.h"
#include "hashtable.h"          // generated by gperf

#define INNER_DEBUG 0

extern YCPValue evaluateIntegerOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);
extern YCPValue evaluateFloatOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);
extern YCPValue evaluateStringOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);
extern YCPValue evaluatePathOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);
extern YCPValue evaluateListOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);
extern YCPValue evaluateMapOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);
extern YCPValue evaluateTermOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);
extern YCPValue evaluateBuiltinOp (YCPInterpreter *interpreter, builtin_t code, const YCPList& args);


// evaluate builtin operator
// the builtin value passed already has all arguments evaluated
// see YCPBasicInterpreter::evaluateBuiltinBuiltin

YCPValue YCPInterpreter::evaluateBuiltinBuiltin (builtin_t code, const YCPList& args)
{
#if INNER_DEBUG
    y2debug ("evaluateBuiltinBuiltin (%d:%s)", (int)code, (args.isNull())?"nil":args->toString().c_str());
#endif
    if (args->size() < 1)
	return YCPNull();

    YCPValue ret = YCPNull();

    // check for boolean operators first
    // these are valid for almost all types

    switch (code)
    {
	case YCPB_EQ:
	{
	    ret = YCPBoolean(args->value(0)->equal(args->value(1)));
	}
	break;
	case YCPB_NEQ:
	{
	    ret = YCPBoolean(! (args->value(0)->equal(args->value(1))));
	}
	break;
	case YCPB_ST:
	{
	    ret = YCPBoolean(args->value(0)->compare(args->value(1)) == YO_LESS);
	}
	break;
	case YCPB_GT:
	{
	    ret = YCPBoolean(args->value(0)->compare(args->value(1)) == YO_GREATER);
	}
	break;
	case YCPB_SE:
	{
	    ret = YCPBoolean(args->value(0)->compare(args->value(1)) != YO_GREATER);
	}
	break;
	case YCPB_GE:
	{
	    ret = YCPBoolean(args->value(0)->compare(args->value(1)) != YO_LESS);
	}
	break;

	case YCPB_FOREACH:
	{
	    if (args->size() == 4 && args->value(2)->isMap())
	    {
		ret = evaluateMapOp (this, code, args);
	    }
	    else if (args->size() == 3 && args->value(1)->isList())
	    {
		ret = evaluateListOp (this, code, args);
	    }
	}
	break;

	case YCPB_SORT:
	{
	    ret = evaluateSort(this, args);
	}
	break;

	case YCPB_NLOCALE:
	{
	    if (args->size() == 3 && args->value(0)->isString() &&
		args->value(1)->isString() && args->value(2)->isInteger())
	    {
		ret = YCPLocale (args->value(0)->asString(),
				 args->value(1)->asString(),
				 args->value(2)->asInteger());
	    }
	    else
	    {
		ret = YCPError ("Wrong args for nlocale");
	    }
	}
        break;

	// type of first argument determines operation

	default:
	{
#if INNER_DEBUG
	    y2debug ("evaluateBuiltinBuiltin by valuetype %d", (int)args->value(0)->valuetype());
#endif
	    switch (args->value(0)->valuetype())
	    {
		case YT_BOOLEAN:
		{
		    if (code == YCPB_NOT)
		    {
		        ret = YCPBoolean(! (args->value(0)->asBoolean()->value()));
		    }
		}
		break;
		case YT_INTEGER:
		    ret = evaluateIntegerOp (this, code, args);
		break;
		case YT_FLOAT:
		    ret = evaluateFloatOp (this, code, args);
		break;
		case YT_STRING:
		    ret = evaluateStringOp (this, code, args);
		break;
		case YT_PATH:
		    ret = evaluatePathOp (this, code, args);
		break;
		case YT_LIST:
		    ret = evaluateListOp (this, code, args);
		break;
		case YT_MAP:
		    ret = evaluateMapOp (this, code, args);
		break;
		case YT_TERM:
		    ret = evaluateTermOp (this, code, args);
		break;
		case YT_BYTEBLOCK:
		    /**
		     * @builtin size (byteblock b) -> integer
		     * Returns the number of bytes in b.
		     */
		    if (code == YCPB_SIZE)
			ret = YCPInteger (args->value(0)->asByteblock()->size());
		break;
		case YT_VOID:		// ignore void silently
		    if (code == YCPB_LOOKUP)
			ret = evaluateMapOp (this, code, args);
		    else
			ret = YCPVoid();
		break;
		case YT_BUILTIN:
		    ret = evaluateBuiltinOp (this, code, args);
		break;
		default:
		    y2error ("Unknown builtin %d for type %d\n", code, (int)(args->value(0)->valuetype()));
		    ret = YCPNull();
		break;
	    }
	}
	break;
    }

    return ret;
}

// check for predefined/builtin term from hashtable.gperf
//

YCPValue YCPInterpreter::evaluateBuiltinTerm(const YCPTerm& term)
{
    string symbol = term->symbol()->symbol();

#if INNER_DEBUG
    y2debug ("evaluateBuiltinTerm (%s)", symbol.c_str());
#endif

    // Try to look up in hashtable.
    const struct hashentry *he = in_word_set (symbol.c_str(), symbol.length());
    if (he)
    {
	YCPValue v = he->evaluate(this, term->args());
	if (!v.isNull() && v->isError())
	{
	    v = evaluate (v);
	}
	return v;
    }
    return YCPNull();
}

// -----------------------------------------------------------------------
