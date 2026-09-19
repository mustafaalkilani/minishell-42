/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   redir_apply.c                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/05 09:14:25 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/08/06 18:23:30 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

static int	open_target(t_redir *redir)
{
	if (redir->type == R_IN)
		return (open(redir->filename, O_RDONLY));
	if (redir->type == R_APPEND)
		return (open(redir->filename, O_WRONLY | O_CREAT | O_APPEND, 0644));
	return (open(redir->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644));
}

static int	apply_one(t_redir *redir)
{
	int	fd;

	if (redir->type == R_HEREDOC)
	{
		if (redir->heredoc_fd < 0)
			return (-1);
		dup2(redir->heredoc_fd, STDIN_FILENO);
		close(redir->heredoc_fd);
		redir->heredoc_fd = -1;
		return (0);
	}
	fd = open_target(redir);
	if (fd < 0)
	{
		shell_error(redir->filename, NULL, strerror(errno));
		return (-1);
	}
	if (redir->type == R_IN)
		dup2(fd, STDIN_FILENO);
	else
		dup2(fd, STDOUT_FILENO);
	close(fd);
	return (0);
}

static void	close_heredoc_list(t_redir *redirs)
{
	while (redirs)
	{
		if (redirs->type == R_HEREDOC && redirs->heredoc_fd >= 0)
		{
			close(redirs->heredoc_fd);
			redirs->heredoc_fd = -1;
		}
		redirs = redirs->next;
	}
}

void	close_other_heredocs(t_cmd *cmds, t_cmd *self)
{
	while (cmds)
	{
		if (cmds != self)
			close_heredoc_list(cmds->redirs);
		cmds = cmds->next;
	}
}

int	apply_redirs(t_cmd *cmd)
{
	t_redir	*cur;

	cur = cmd->redirs;
	while (cur)
	{
		if (apply_one(cur) < 0)
			return (-1);
		cur = cur->next;
	}
	return (0);
}
