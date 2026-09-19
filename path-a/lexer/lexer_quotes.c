/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   lexer_quotes.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/08 12:41:44 by malkilan          #+#    #+#             */
/*   Updated: 2026/09/08 16:17:51 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

static int	quote_step(char c, t_quote *state)
{
	if (c == '\'' && *state != Q_DOUBLE)
	{
		if (*state == Q_NONE)
			*state = Q_SINGLE;
		else
			*state = Q_NONE;
		return (1);
	}
	if (c == '"' && *state != Q_SINGLE)
	{
		if (*state == Q_NONE)
			*state = Q_DOUBLE;
		else
			*state = Q_NONE;
		return (1);
	}
	return (0);
}

int	word_measure(const char *s, size_t i, size_t *end)
{
	t_quote	state;
	int		len;

	state = Q_NONE;
	len = 0;
	while (s[i])
	{
		if (state == Q_NONE && (is_space(s[i]) || is_metachar(s[i])))
			break ;
		if (!quote_step(s[i], &state))
			len++;
		i++;
	}
	if (state != Q_NONE)
		return (-1);
	*end = i;
	return (len);
}

void	word_fill(const char *s, size_t i, char *value, char *quotes)
{
	t_quote	state;
	size_t	w;
	char	brk;

	state = Q_NONE;
	w = 0;
	brk = 0;
	while (s[i])
	{
		if (state == Q_NONE && (is_space(s[i]) || is_metachar(s[i])))
			break ;
		if (quote_step(s[i], &state))
			brk = Q_BREAK;
		else
		{
			value[w] = s[i];
			quotes[w] = (char)state | brk;
			w++;
			brk = 0;
		}
		i++;
	}
	value[w] = '\0';
	quotes[w] = '\0';
}
