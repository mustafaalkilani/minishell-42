/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   expander_split.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

/* Whitespace separates arguments only when it arrived through an
** unquoted expansion. That is the whole difference between $X and "$X". */
static int	is_split(t_exp *e, size_t i)
{
	return (e->mask[i] == '1' && is_space(e->out[i]));
}

/* Skips separators, then reports the bounds of the next field. Returns 0
** once the text is exhausted, so trailing separators yield no field. */
static int	field_next(t_exp *e, size_t *i, size_t *start)
{
	while (e->out[*i] && is_split(e, *i))
		(*i)++;
	if (!e->out[*i])
		return (0);
	*start = *i;
	while (e->out[*i] && !is_split(e, *i))
		(*i)++;
	return (1);
}

/* Replaces a token's text, taking ownership of value. */
static int	token_set(t_token *tok, char *value)
{
	if (!value)
		return (-1);
	free(tok->value);
	tok->value = value;
	return (0);
}

/* Links a fresh word token after *tok and moves *tok onto it, so the
** caller ends up pointing at the last field it produced. The quotes
** array is allocated but left zeroed: expansion is already done, and
** free_tokens still expects something to free. */
static int	insert_after(t_token **tok, char *value)
{
	t_token	*node;
	char	*quotes;

	quotes = NULL;
	if (value)
		quotes = ft_calloc(ft_strlen(value) + 1, 1);
	if (!value || !quotes)
	{
		free(value);
		free(quotes);
		return (-1);
	}
	node = token_new(T_WORD, value, quotes);
	if (!node)
		return (-1);
	node->next = (*tok)->next;
	(*tok)->next = node;
	*tok = node;
	return (0);
}

/* One word can expand into several arguments, so extra fields are
** spliced into the token list right after the token being expanded.
** A result with no fields at all stays as an empty token: the parser
** then uses had_quotes to tell "" (keep) from $NOPE (drop). */
int	split_token(t_token **tok, t_exp *e)
{
	size_t	i;
	size_t	start;

	i = 0;
	if (!field_next(e, &i, &start))
		return (token_set(*tok, ft_strdup("")));
	if (token_set(*tok, ft_substr(e->out, (unsigned int)start, i - start)) < 0)
		return (-1);
	while (field_next(e, &i, &start))
	{
		if (insert_after(tok, ft_substr(e->out, (unsigned int)start,
					i - start)) < 0)
			return (-1);
	}
	return (0);
}
