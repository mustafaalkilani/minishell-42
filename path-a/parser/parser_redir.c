/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parser_redir.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

static t_redir_type	redir_type_of(t_token_type type)
{
	if (type == T_REDIR_IN)
		return (R_IN);
	if (type == T_APPEND)
		return (R_APPEND);
	if (type == T_HEREDOC)
		return (R_HEREDOC);
	return (R_OUT);
}

/* quoted_delim only matters for heredocs: <<"EOF" and <<'EOF' keep the
** body literal, while <<EOF expands variables inside it. heredoc_fd is
** filled in later by path-b, before the pipeline forks. */
static t_redir	*redir_new(t_token *op, t_token *target)
{
	t_redir	*redir;

	redir = malloc(sizeof(t_redir));
	if (!redir)
		return (NULL);
	redir->filename = ft_strdup(target->value);
	if (!redir->filename)
	{
		free(redir);
		return (NULL);
	}
	redir->type = redir_type_of(op->type);
	redir->quoted_delim = target->had_quotes;
	redir->heredoc_fd = -1;
	redir->next = NULL;
	return (redir);
}

int	cmd_add_redir(t_cmd *cmd, t_token *op, t_token *target)
{
	t_redir	*redir;
	t_redir	*cur;

	redir = redir_new(op, target);
	if (!redir)
		return (-1);
	if (!cmd->redirs)
	{
		cmd->redirs = redir;
		return (0);
	}
	cur = cmd->redirs;
	while (cur->next)
		cur = cur->next;
	cur->next = redir;
	return (0);
}

/* A redirection operator must be followed by a word. Anything else is
** the classic "syntax error near unexpected token" bash reports. */
int	parse_redir_step(t_cmd *cmd, t_token **tokens)
{
	t_token	*op;
	t_token	*target;

	op = *tokens;
	target = op->next;
	if (!target)
		return (syntax_error("newline"));
	if (target->type != T_WORD)
		return (syntax_error(target->value));
	if (cmd_add_redir(cmd, op, target) < 0)
		return (-1);
	*tokens = target->next;
	return (0);
}
