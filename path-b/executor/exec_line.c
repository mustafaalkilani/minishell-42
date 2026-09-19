/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   exec_line.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/29 20:53:29 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/09/02 17:58:25 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

static void	pipeline_child(t_cmd *cmd, t_shell *sh, t_pipeline *pl)
{
	if (pl->prev_read != -1)
	{
		dup2(pl->prev_read, STDIN_FILENO);
		close(pl->prev_read);
	}
	if (cmd->next)
	{
		close(pl->pipefd[0]);
		dup2(pl->pipefd[1], STDOUT_FILENO);
		close(pl->pipefd[1]);
	}
	close_other_heredocs(sh->current_cmds, cmd);
	child_exec(cmd, sh);
}

static void	pipeline_parent(t_cmd *cmd, t_pipeline *pl)
{
	if (pl->prev_read != -1)
		close(pl->prev_read);
	if (cmd->next)
	{
		close(pl->pipefd[1]);
		pl->prev_read = pl->pipefd[0];
	}
	else
		pl->prev_read = -1;
}

static int	run_pipeline(t_cmd *cmds, t_shell *sh)
{
	t_pipeline	pl;
	pid_t		pid;

	pl.prev_read = -1;
	pl.last_pid = -1;
	signals_ignore();
	while (cmds)
	{
		if (cmds->next && pipe(pl.pipefd) < 0)
			return (1);
		pid = fork();
		if (pid < 0)
			return (1);
		if (pid == 0)
			pipeline_child(cmds, sh, &pl);
		pl.last_pid = pid;
		pipeline_parent(cmds, &pl);
		cmds = cmds->next;
	}
	return (wait_for_all(pl.last_pid));
}

static int	run_parent_builtin(t_cmd *cmd, t_shell *sh)
{
	int	saved_in;
	int	saved_out;
	int	status;

	saved_in = dup(STDIN_FILENO);
	saved_out = dup(STDOUT_FILENO);
	if (apply_redirs(cmd) < 0)
		status = 1;
	else
		status = run_builtin(cmd, sh);
	dup2(saved_in, STDIN_FILENO);
	dup2(saved_out, STDOUT_FILENO);
	close(saved_in);
	close(saved_out);
	return (status);
}

int	exec_line(t_cmd *cmds, t_shell *sh)
{
	sh->current_cmds = cmds;
	if (collect_heredocs(cmds, sh) < 0)
		return (EXIT_SIG_BASE + SIGINT);
	if (!cmds->next && cmds->argv[0] && is_builtin(cmds->argv[0]))
		return (run_parent_builtin(cmds, sh));
	if (!cmds->next && !cmds->argv[0] && !cmds->redirs)
		return (EXIT_OK);
	return (run_pipeline(cmds, sh));
}
