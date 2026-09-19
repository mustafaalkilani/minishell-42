/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   lexer_ops.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/07 09:27:02 by malkilan          #+#    #+#             */
/*   Updated: 2026/09/07 12:38:01 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

static t_token_type	op_type(const char *s, size_t i, size_t *len)
{
	*len = 1;
	if (s[i] == '|')
		return (T_PIPE);
	if (s[i] == '<' && s[i + 1] == '<')
	{
		*len = 2;
		return (T_HEREDOC);
	}
	if (s[i] == '>' && s[i + 1] == '>')
	{
		*len = 2;
		return (T_APPEND);
	}
	if (s[i] == '<')
		return (T_REDIR_IN);
	return (T_REDIR_OUT);
}

t_token	*read_operator(const char *s, size_t *i)
{
	t_token_type	type;
	size_t			len;
	char			*text;

	type = op_type(s, *i, &len);
	text = ft_substr(s, (unsigned int)*i, len);
	if (!text)
		return (NULL);
	*i += len;
	return (token_new(type, text, NULL));
}
