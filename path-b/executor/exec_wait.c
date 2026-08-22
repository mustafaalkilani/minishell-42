/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   exec_wait.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* A command killed by a signal reports 128+signum. ctrl-C therefore
** yields 130 and ctrl-\ yields 131, matching bash. */
int	status_to_exit(int status)
{
	if (WIFSIGNALED(status))
		return (EXIT_SIG_BASE + WTERMSIG(status));
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (EXIT_OK);
}

/* Prints the newline or message the terminal would otherwise miss when
** a foreground child dies from a signal, so the next prompt lines up. */
static void	report_signal(int status)
{
	int	sig;

	if (!WIFSIGNALED(status))
		return ;
	sig = WTERMSIG(status);
	if (sig == SIGINT)
		write(STDOUT_FILENO, "\n", 1);
	else if (sig == SIGQUIT)
		write(STDOUT_FILENO, "Quit (core dumped)\n", 19);
}

/* Reaps every child but only the last one decides $?, which is the
** pipeline semantics bash uses. Waiting for all of them prevents
** zombies even when an early command exits first. */
int	wait_for_all(pid_t last_pid)
{
	int		status;
	int		last_status;
	pid_t	pid;

	last_status = EXIT_OK;
	pid = waitpid(-1, &status, 0);
	while (pid > 0)
	{
		if (pid == last_pid)
		{
			last_status = status_to_exit(status);
			report_signal(status);
		}
		pid = waitpid(-1, &status, 0);
	}
	return (last_status);
}
