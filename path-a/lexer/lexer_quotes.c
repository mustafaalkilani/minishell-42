/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   lexer_quotes.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

/* Advances the quote state machine by one character. Returns 1 when the
** character is a quote delimiter, which means it is consumed but never
** written to the output. A single quote inside double quotes (and vice
** versa) is an ordinary character, hence the state guards. */
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

/* First of two passes over a word. Counts how many characters survive
** once quote delimiters are dropped, so the caller can malloc exactly
** once instead of growing a buffer. Returns -1 if a quote is never
** closed, which the subject treats as a syntax error rather than
** prompting for more input the way bash does. */
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

/* Second pass. Writes each surviving character together with the quote
** state that was active when it was read, so the expander can later tell
** $VAR (expand) from '$VAR' (literal). Characters that follow a dropped
** delimiter also carry Q_BREAK, which is the only remaining trace that
** a quote once stood between them and the character before. */
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
