/*
 * Copyright (c) 2012 BalaBit IT Ltd, Budapest, Hungary
 * Copyright (c) 2012 Gergely Nagy <algernon@balabit.hu>,
 *                    Peter Gyongyosi <gyp@balabit.hu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 */

#include "plugin.h"
#include "template/templates.h"
#include "cfg.h"
#include "uuid.h"
#include "misc.h"
#include "str-format.h"

#if ENABLE_SSL
#include <openssl/evp.h>
#endif

static void
tf_uuid(LogMessage *msg, gint argc, GString *argv[], GString *result)
{
  char uuid_str[37];

  uuid_gen_random(uuid_str, sizeof(uuid_str));
  g_string_append (result, uuid_str);
}

TEMPLATE_FUNCTION_SIMPLE(tf_uuid);

#if ENABLE_SSL
/*
 * $($hash_method [opts] $arg1 $arg2 $arg3...)
 *
 * Returns the hash of the argument, using the specified hashing
 * method. Note that the values of the arguments are simply concatenated
 * when calculating the hash.
 *
 * Options:
 *      --length N, -l N    Truncate the hash to the first N characters
 */
typedef struct _TFHashState
{
  TFSimpleFuncState super;
  gint length;
  const EVP_MD *md;
} TFHashState;

static gboolean
tf_hash_prepare(LogTemplateFunction *self, LogTemplate *parent, gint argc, gchar *argv[], gpointer *s, GDestroyNotify *state_destroy, GError **error)
{
  TFHashState *state;
  GOptionContext *ctx;
  gint length = 0;
  const EVP_MD *md;

  GOptionEntry hash_options[] = {
    { "length", 'l', 0, G_OPTION_ARG_INT, &length, NULL, NULL },
    { NULL }
  };

  ctx = g_option_context_new("hash");
  g_option_context_add_main_entries(ctx, hash_options, NULL);

  if (!g_option_context_parse(ctx, &argc, &argv, error))
    {
      g_option_context_free(ctx);
      return FALSE;
    }
  g_option_context_free(ctx);


  if (argc < 2)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE, "$(hash) parsing failed, invalid number of arguments");
      return FALSE;
    }

  if (length < 0)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE, "$(hash) parsing failed, negative value for length");
      return FALSE;
    }

  state = g_new0(TFHashState, 1);

  /* First argument is template function name, do not try to copile it */
  if (!tf_simple_func_compile_templates(parent, argc - 1, &argv[1], &state->super, error))
    {
      tf_simple_func_free_state(state);
      return FALSE;
    }
  state->length = length;
  md = EVP_get_digestbyname(strcmp(argv[0], "hash") ? argv[0] : "sha256" );
  if (!md)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE, "$(hash) parsing failed, unknown digest type");
      return FALSE;
    }
  state->md = md;
  if ((state->length == 0) || (state->length > md->md_size * 2))
    state->length = md->md_size * 2;
  *s = (gpointer) state;
  *state_destroy = tf_simple_func_free_state;
  return TRUE;
}

static void
tf_hash_call(LogTemplateFunction *self, gpointer s, GPtrArray *arg_bufs,
             LogMessage **messages, gint num_messages, LogTemplateOptions *opts,
             gint tz, gint seq_num, const gchar *context_id, GString *result)
{
  TFHashState *state = (TFHashState *) s;
  GString **argv;
  gint argc;
  gint i;
  EVP_MD_CTX mdctx;
  guchar hash[EVP_MAX_MD_SIZE];
  gchar hash_str[EVP_MAX_MD_SIZE * 2 + 1];
  guint md_len;

  argv = (GString **) arg_bufs->pdata;
  argc = arg_bufs->len;

  EVP_MD_CTX_init(&mdctx);
  EVP_DigestInit_ex(&mdctx, state->md, NULL);

  for (i = 0; i < argc; i++)
    {
      EVP_DigestUpdate(&mdctx, argv[i]->str, argv[i]->len);
    }
  EVP_DigestFinal_ex(&mdctx, hash, &md_len);
  EVP_MD_CTX_cleanup(&mdctx);

  // we fetch the entire hash in a hex format otherwise we cannot truncate at
  // odd character numbers
  format_hex_string(hash, md_len, hash_str, sizeof(hash_str));
  if (state->length == 0)
    {
      g_string_append(result, hash_str);
    }
  else
    {
      g_string_append_len(result, hash_str, MIN(sizeof(hash_str), state->length));
    }
}

TEMPLATE_FUNCTION(tf_hash, tf_hash_prepare, tf_simple_func_eval, tf_hash_call, NULL );

#endif

static Plugin cryptofuncs_plugins[] =
{
  TEMPLATE_FUNCTION_PLUGIN(tf_uuid, "uuid"),
#if ENABLE_SSL
  TEMPLATE_FUNCTION_PLUGIN(tf_hash, "hash"),
  TEMPLATE_FUNCTION_PLUGIN(tf_hash, "sha1"),
  TEMPLATE_FUNCTION_PLUGIN(tf_hash, "sha256"),
  TEMPLATE_FUNCTION_PLUGIN(tf_hash, "sha512"),
  TEMPLATE_FUNCTION_PLUGIN(tf_hash, "md4"),
  TEMPLATE_FUNCTION_PLUGIN(tf_hash, "md5"),
#endif
};

gboolean
cryptofuncs_module_init(GlobalConfig *cfg, CfgArgs *args)
{
  plugin_register(cfg, cryptofuncs_plugins, G_N_ELEMENTS(cryptofuncs_plugins));
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "cryptofuncs",
  .version = VERSION,
  .description = "The cryptofuncs module provides cryptographic template functions.",
  .core_revision = SOURCE_REVISION,
  .plugins = cryptofuncs_plugins,
  .plugins_len = G_N_ELEMENTS(cryptofuncs_plugins),
};