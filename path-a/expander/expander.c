/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   expander.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

/* value and quotes point at the '$'. A name that does not start a valid
** identifier leaves the $ as a literal character, exactly like bash.
** Returns how many characters of the word were consumed. */
static size_t	append_named(t_exp *e, const char *value, const char *quotes,
		char flag)
{
	char	*name;
	int		len;

	len = var_name_len(value + 1, quotes + 1);
	if (len == 0)
	{
		exp_append(e, ft_strdup("$"), '0');
		return (1);
	}
	name = ft_substr(value, 1, (size_t)len);
	if (!name)
		exp_append(e, NULL, flag);
	else
	{
		exp_append(e, var_lookup(name, e->sh), flag);
		free(name);
	}
	return (1 + (size_t)len);
}

/* Consumes one $-expansion and advances i past it. Four cases: $? is
** shell state, ${NAME} is a braced lookup, a bare name is looked up in
** env, and a $ before a quote is dropped. What the variable produced is
** marked splittable only when the $ itself was unquoted. */
static void	append_var(t_exp *e, const char *value, size_t *i,
		const char *quotes)
{
	char	flag;

	flag = '0';
	if ((quotes[*i] & Q_MASK) == Q_NONE)
		flag = '1';
	if (is_quote_prefix(quotes, *i))
	{
		*i += 1;
		return ;
	}
	if (value[*i + 1] == '?')
	{
		*i += 2;
		return (exp_append(e, ft_itoa(e->sh->last_status), flag));
	}
	if (value[*i + 1] == '{')
		*i += append_braced(e, value + *i, quotes + *i, flag);
	else
		*i += append_named(e, value + *i, quotes + *i, flag);
}

/* Walks the word copying literal runs and substituting variables. The
** quotes array decides expansion: inside single quotes a $ is data, in
** double quotes and unquoted text it introduces a variable. Literal
** runs are never splittable, because the lexer already ended the word
** at any unquoted whitespace. tilde is off for heredoc bodies, where
** bash leaves a leading ~ alone. */
static t_exp	expand_masked(const char *value, const char *quotes,
		t_shell *sh, int tilde)
{
	t_exp	e;
	size_t	i;
	size_t	start;

	e.out = ft_strdup("");
	e.mask = ft_strdup("");
	e.sh = sh;
	i = tilde_prefix(&e, value, quotes, tilde);
	start = i;
	while (value[i])
	{
		if (value[i] == '$' && (quotes[i] & Q_MASK) != Q_SINGLE
			&& value[i + 1])
		{
			exp_append(&e, ft_substr(value, (unsigned int)start,
					i - start), '0');
			append_var(&e, value, &i, quotes);
			start = i;
		}
		else
			i++;
	}
	exp_append(&e, ft_substr(value, (unsigned int)start, i - start), '0');
	return (e);
}

/* A heredoc body is expanded but never split into fields, so its mask is
** thrown away. This is the only entry point path-b needs. */
char	*expand_word(const char *value, const char *quotes, t_shell *sh)
{
	t_exp	e;

	e = expand_masked(value, quotes, sh, 0);
	free(e.mask);
	return (e.out);
}

/* Heredoc delimiters are deliberately skipped: bash does not expand the
** delimiter itself, and whether it was quoted decides if the heredoc
** body gets expanded later, which the parser records separately. */
int	expand(t_token *tokens, t_shell *sh)
{
	t_token	*prev;
	t_exp	e;

	prev = NULL;
	while (tokens)
	{
		if (tokens->type == T_WORD && (!prev || prev->type != T_HEREDOC))
		{
			e = expand_masked(tokens->value, tokens->quotes, sh, 1);
			if (!e.out || split_token(&tokens, &e) < 0)
			{
				free(e.out);
				free(e.mask);
				return (-1);
			}
			free(e.out);
			free(e.mask);
		}
		prev = tokens;
		tokens = tokens->next;
	}
	return (0);
}
