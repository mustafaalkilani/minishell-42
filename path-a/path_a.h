/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   path_a.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef PATH_A_H
# define PATH_A_H

# include "minishell.h"

/* Result of expanding one word. out is the expanded text; mask holds one
** byte per character of out, '1' when that character came from an
** unquoted expansion. Only those characters may split the word into
** several arguments, which is what keeps "$X" a single argument. */
typedef struct s_exp
{
	char	*out;
	char	*mask;
	t_shell	*sh;
}	t_exp;

/* lexer internals */
t_token			*token_new(t_token_type type, char *value, char *quotes);
void			token_add_back(t_token **head, t_token *tok);
int				word_measure(const char *s, size_t i, size_t *end);
void			word_fill(const char *s, size_t i, char *value, char *quotes);
t_token			*read_operator(const char *s, size_t *i);

/* parser internals */
t_cmd			*cmd_new(void);
int				cmd_add_arg(t_cmd *cmd, char *value);
int				cmd_add_redir(t_cmd *cmd, t_token *op, t_token *target);
int				parse_redir_step(t_cmd *cmd, t_token **tokens);
void			free_redirs(t_redir *redirs);
int				syntax_error(const char *near);

/* expander internals */
char			*var_lookup(const char *name, t_shell *sh);
int				var_name_len(const char *s, const char *quotes);
char			*join_free(char *dst, char *src);
char			*pad_new(size_t n, char flag);
void			exp_append(t_exp *e, char *chunk, char flag);
int				split_token(t_token **tok, t_exp *e);
size_t			tilde_prefix(t_exp *e, const char *value, const char *quotes,
					int enabled);
int				is_quote_prefix(const char *quotes, size_t i);

#endif
