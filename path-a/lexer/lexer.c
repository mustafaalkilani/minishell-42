/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   lexer.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 20:10:44 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/05 11:48:21 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

/* A word is "quoted" if a quote delimiter appeared anywhere in its raw
** text. The surviving characters cannot answer this, because '' and ""
** leave nothing behind yet still produce an argument bash keeps. */
static int	scan_had_quotes(const char *s, size_t start, size_t end)
{
	while (start < end)
	{
		if (s[start] == '\'' || s[start] == '"')
			return (1);
		start++;
	}
	return (0);
}

/* Measure-then-fill avoids any buffer growth logic. A length of 0 with a
** consumed range is legitimate: it is the empty word produced by "" or
** '', which bash keeps as an argument. */
static t_token	*read_word(const char *s, size_t *i)
{
	size_t	end;
	int		len;
	char	*value;
	char	*quotes;

	len = word_measure(s, *i, &end);
	if (len < 0)
	{
		shell_error(NULL, NULL, "syntax error: unclosed quote");
		return (NULL);
	}
	value = malloc((size_t)len + 1);
	quotes = ft_calloc((size_t)len + 1, 1);
	if (!value || !quotes)
	{
		free(value);
		free(quotes);
		return (NULL);
	}
	word_fill(s, *i, value, quotes);
	*i = end;
	return (token_new(T_WORD, value, quotes));
}

static t_token	*next_token(const char *input, size_t *i)
{
	t_token	*tok;
	size_t	start;

	if (is_metachar(input[*i]))
		return (read_operator(input, i));
	start = *i;
	tok = read_word(input, i);
	if (tok)
		tok->had_quotes = scan_had_quotes(input, start, *i);
	return (tok);
}

/* Returns NULL both for an all-blank line and for a syntax error. main()
** filters blank lines beforehand so the distinction never matters. */
t_token	*lex(const char *input)
{
	t_token	*head;
	t_token	*tok;
	size_t	i;

	head = NULL;
	i = 0;
	while (input[i])
	{
		while (input[i] && is_space(input[i]))
			i++;
		if (!input[i])
			break ;
		tok = next_token(input, &i);
		if (!tok)
		{
			free_tokens(head);
			return (NULL);
		}
		token_add_back(&head, tok);
	}
	return (head);
}
