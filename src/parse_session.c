/**
 * @file exec_parse_session.c
 * @brief Implementation of the unified parse session.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "parse_session.h"

#include "lexer.h"
#include "token.h"
#include "tokenizer.h"
#include "miga/string_t.h"
#include "miga/xalloc.h"

#include <string.h>

 /* ============================================================================
  * Lifecycle
  * ============================================================================ */

parse_session_t* parse_session_create(alias_store_t* aliases)
{
    parse_session_t* s = xcalloc(1, sizeof(*s));

    s->lexer = lexer_create();
    if (!s->lexer)
    {
        xfree(s);
        return NULL;
    }

    if (aliases)
    {
        s->own_aliases = false;
        s->aliases = aliases;
    }
    else
    {
        s->own_aliases = true;
        s->aliases = alias_store_create();
    }
    s->tokenizer = tokenizer_create(s->aliases);
    s->accumulated_tokens = NULL;
    s->line_num = 0;
    s->incomplete = false;
    s->filename = NULL;
    s->caller_line_number = 0;

    return s;
}

void parse_session_destroy(parse_session_t** session)
{
    if (!session || !*session)
        return;

    parse_session_t* s = *session;

    if (s->lexer)
        lexer_destroy(&s->lexer);
    if (s->own_aliases && s->aliases)
    {
        alias_store_destroy(&s->aliases);
        s->own_aliases = false;
    }
    if (s->tokenizer)
        tokenizer_destroy(&s->tokenizer);
    if (s->accumulated_tokens)
        token_list_destroy(&s->accumulated_tokens);
    if (s->filename)
        string_destroy(&s->filename);

    xfree(s);
    *session = NULL;
}

void parse_session_reset(parse_session_t* session)
{
    if (!session)
        return;

    if (session->lexer)
        lexer_reset(session->lexer);
    if (session->accumulated_tokens)
    {
        token_list_destroy(&session->accumulated_tokens);
        session->accumulated_tokens = NULL;
    }
    if (session->tokenizer)
        tokenizer_reset(session->tokenizer);

    session->incomplete = false;
}

void parse_session_hard_reset(parse_session_t* session, alias_store_t* aliases)
{
    if (!session)
        return;

    /* Reset the lexer. */
    if (session->lexer)
        lexer_reset(session->lexer);

    /* Discard accumulated tokens. */
    if (session->accumulated_tokens)
    {
        token_list_destroy(&session->accumulated_tokens);
        session->accumulated_tokens = NULL;
    }

    /* Destroy and recreate the tokenizer to flush any buffered
       compound-command tokens. */
    if (session->tokenizer)
        tokenizer_destroy(&session->tokenizer);

    if (!session->own_aliases)
    {
        if (aliases == NULL)
        {
            session->aliases = alias_store_create();
            session->own_aliases = true;
        }
        else
        {
            session->aliases = aliases;
            session->own_aliases = false;
        }
    }
    else
    {
        if (aliases == NULL)
            alias_store_clear(session->aliases);
        else
        {
            alias_store_destroy(&session->aliases);
            session->aliases = aliases;
            session->own_aliases = false;
        }
    }
    session->tokenizer = tokenizer_create(session->aliases);

    session->incomplete = false;
}

/* ============================================================================
 * Opaque-size helpers
 * ============================================================================ */

size_t parse_session_size(void)
{
    return sizeof(parse_session_t);
}
