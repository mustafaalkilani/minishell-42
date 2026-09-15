/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/08 15:55:54 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/08 18:34:53 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "minishell.h"

/* Mirrors bash's "bash: ctx: arg: message" error shape. Both ctx and arg
** are optional so one helper covers "minishell: cd: /nope: No such file"
** and the shorter "minishell: syntax error near unexpected token `|'". */
void	shell_error(const char *ctx, const char *arg, const char *msg)
{
	ft_putstr_fd("minishell: ", STDERR_FILENO);
	if (ctx)
	{
		ft_putstr_fd((char *)ctx, STDERR_FILENO);
		ft_putstr_fd(": ", STDERR_FILENO);
	}
	if (arg)
	{
		ft_putstr_fd((char *)arg, STDERR_FILENO);
		ft_putstr_fd(": ", STDERR_FILENO);
	}
	ft_putendl_fd((char *)msg, STDERR_FILENO);
}

int	is_space(char c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\v'
		|| c == '\f' || c == '\r');
}

/* Characters that terminate a word and start an operator token. The
** subject explicitly excludes ; and \ from what we must interpret. */
int	is_metachar(char c)
{
	return (c == '|' || c == '<' || c == '>');
}

int	line_is_blank(const char *line)
{
	while (*line)
	{
		if (!is_space(*line))
			return (0);
		line++;
	}
	return (1);
}
