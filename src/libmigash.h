/* libmigash.h - the public API for the Miga Shell Library */
#ifndef MIGASH_H
#define MIGASH_H

/* Must come first */
#include "miga/migaconf.h"

/* The allocator */
#include "miga/mutex.h"
#include "miga/xalloc.h"

/* Primitive types */
#include "miga/string_t.h"
#include "miga/strlist.h"

/* Getopt for shell builtins */
#include "miga/getopt.h"

/* Shell execution API */
#include "miga/type_pub.h"
#include "miga/frame.h"
#include "miga/exec.h"

#endif /* MIGASH_H */
