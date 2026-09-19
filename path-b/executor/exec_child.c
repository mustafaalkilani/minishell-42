/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   exec_child.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/25 12:40:54 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/08/26 00:21:42 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

static void	execve_failed(char *cmd)
{
	struct stat	st;

	if (errno == EISDIR
		|| (errno == EACCES && stat(cmd, &st) == 0 && S_ISDIR(st.st_mode)))
	{
		shell_error(cmd, NULL, "Is a directory");
		exit(EXIT_CANT_EXEC);
	}
	if (errno == EACCES)
	{
		shell_error(cmd, NULL, "Permission denied");
		exit(EXIT_CANT_EXEC);
	}
	shell_error(cmd, NULL, strerror(errno));
	exit(EXIT_NOT_FOUND);
}

void	child_exec(t_cmd *cmd, t_shell *sh)
{
	char	*path;
	char	**envp;

	signals_setup_child();
	if (apply_redirs(cmd) < 0)
		exit(1);
	if (!cmd->argv[0])
		exit(EXIT_OK);
	if (is_builtin(cmd->argv[0]))
		exit(run_builtin(cmd, sh));
	path = resolve_command(cmd->argv[0], sh);
	if (!path)
	{
		shell_error(cmd->argv[0], NULL, "command not found");
		exit(EXIT_NOT_FOUND);
	}
	envp = env_to_envp(sh->env);
	execve(path, cmd->argv, envp);
	free(path);
	free_split(envp);
	execve_failed(cmd->argv[0]);
}
