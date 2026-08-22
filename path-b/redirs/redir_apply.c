/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   redir_apply.c                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* O_TRUNC for > and O_APPEND for >> are the only difference between the
** two output forms; both create the file with 0644 when missing. */
static int	open_target(t_redir *redir)
{
	if (redir->type == R_IN)
		return (open(redir->filename, O_RDONLY));
	if (redir->type == R_APPEND)
		return (open(redir->filename, O_WRONLY | O_CREAT | O_APPEND, 0644));
	return (open(redir->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644));
}

/* A heredoc was already drained into a pipe before the fork, so there is
** no file to open: the stored read end simply becomes stdin. */
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

/* Every heredoc on the line is drained before the first fork, so a child
** also inherits the read ends belonging to the other pipeline stages.
** Nothing closes those after execve, so they would show up as stray fds
** in the executed program. Only the stage's own heredoc is kept. */
void	close_other_heredocs(t_cmd *cmds, t_cmd *self)
{
	while (cmds)
	{
		if (cmds != self)
			close_heredoc_list(cmds->redirs);
		cmds = cmds->next;
	}
}

/* Redirections are applied left to right, so `> a > b` leaves stdout on
** b while still creating a, exactly like bash. */
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
