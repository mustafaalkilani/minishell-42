/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parser.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

int	syntax_error(const char *near)
{
	ft_putstr_fd("minishell: syntax error near unexpected token `",
		STDERR_FILENO);
	ft_putstr_fd((char *)near, STDERR_FILENO);
	ft_putendl_fd("'", STDERR_FILENO);
	return (-1);
}

void	free_cmds(t_cmd *cmds)
{
	t_cmd	*next;
	size_t	i;

	while (cmds)
	{
		next = cmds->next;
		i = 0;
		while (cmds->argv && cmds->argv[i])
		{
			free(cmds->argv[i]);
			i++;
		}
		free(cmds->argv);
		free_redirs(cmds->redirs);
		free(cmds);
		cmds = next;
	}
}

/* A pipe needs something on its left (command or redirection) and at
** least one more token on its right, otherwise bash reports a syntax
** error rather than waiting for more input as it does interactively. */
static int	parse_pipe(t_cmd **cur, t_token **tokens)
{
	t_cmd	*next;

	if (!(*cur)->argv[0] && !(*cur)->redirs)
		return (syntax_error("|"));
	if (!(*tokens)->next)
		return (syntax_error("newline"));
	next = cmd_new();
	if (!next)
		return (-1);
	(*cur)->next = next;
	*cur = next;
	*tokens = (*tokens)->next;
	return (0);
}

/* An unquoted word that expanded to nothing disappears entirely, so
** `echo $NOPE` runs echo with no arguments. A quoted one survives as an
** empty string, so `echo "$NOPE"` prints a blank line. */
static int	parse_step(t_cmd **cur, t_token **tokens)
{
	t_token	*tok;
	char	*arg;

	tok = *tokens;
	if (tok->type == T_PIPE)
		return (parse_pipe(cur, tokens));
	if (tok->type != T_WORD)
		return (parse_redir_step(*cur, tokens));
	if (tok->value[0] || tok->had_quotes)
	{
		arg = ft_strdup(tok->value);
		if (!arg || cmd_add_arg(*cur, arg) < 0)
			return (-1);
	}
	*tokens = tok->next;
	return (0);
}

t_cmd	*parse(t_token *tokens)
{
	t_cmd	*head;
	t_cmd	*cur;

	head = cmd_new();
	if (!head)
		return (NULL);
	cur = head;
	while (tokens)
	{
		if (parse_step(&cur, &tokens) < 0)
		{
			free_cmds(head);
			return (NULL);
		}
	}
	return (head);
}
