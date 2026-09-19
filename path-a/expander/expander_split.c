/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   expander_split.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/07 22:09:40 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/08 03:31:52 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

static int	is_split(t_exp *e, size_t i)
{
	return (e->mask[i] == '1' && is_space(e->out[i]));
}

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

static int	token_set(t_token *tok, char *value)
{
	if (!value)
		return (-1);
	free(tok->value);
	tok->value = value;
	return (0);
}

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
