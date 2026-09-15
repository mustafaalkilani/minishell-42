/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   lexer_utils.c                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/10 09:06:22 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/14 13:02:46 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

/* Takes ownership of value and quotes, so every failure path frees them
** here. That keeps the callers free of cleanup branching. quotes may be
** NULL for operator tokens, which carry no quote metadata. */
t_token	*token_new(t_token_type type, char *value, char *quotes)
{
	t_token	*tok;

	if (!value)
	{
		free(quotes);
		return (NULL);
	}
	tok = malloc(sizeof(t_token));
	if (!tok)
	{
		free(value);
		free(quotes);
		return (NULL);
	}
	tok->type = type;
	tok->value = value;
	tok->quotes = quotes;
	tok->had_quotes = 0;
	tok->next = NULL;
	return (tok);
}

void	token_add_back(t_token **head, t_token *tok)
{
	t_token	*cur;

	if (!*head)
	{
		*head = tok;
		return ;
	}
	cur = *head;
	while (cur->next)
		cur = cur->next;
	cur->next = tok;
}

void	free_tokens(t_token *tokens)
{
	t_token	*next;

	while (tokens)
	{
		next = tokens->next;
		free(tokens->value);
		free(tokens->quotes);
		free(tokens);
		tokens = next;
	}
}
