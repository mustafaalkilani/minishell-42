/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   exec_path.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

void	free_split(char **split)
{
	int	i;

	if (!split)
		return ;
	i = 0;
	while (split[i])
	{
		free(split[i]);
		i++;
	}
	free(split);
}

/* Directories carry the execute bit as "searchable", so access(X_OK)
** alone would happily accept /usr/bin/. as a command and then fail in
** execve with the wrong exit code. */
static int	is_executable_file(const char *path)
{
	struct stat	st;

	if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
		return (0);
	return (access(path, X_OK) == 0);
}

/* Walks PATH entries in order and returns the first executable match,
** which is exactly how the shell picks between /usr/bin/ls and a local
** override earlier in PATH. */
static char	*search_in_path(char *cmd, char **paths)
{
	char	*full_path;
	char	*temp;
	int		i;

	i = 0;
	while (paths[i])
	{
		temp = ft_strjoin(paths[i], "/");
		if (!temp)
			return (NULL);
		full_path = ft_strjoin(temp, cmd);
		free(temp);
		if (!full_path)
			return (NULL);
		if (is_executable_file(full_path))
			return (full_path);
		free(full_path);
		i++;
	}
	return (NULL);
}

/* A name containing '/' is used verbatim and never searched on PATH,
** matching the shell rule that ./cmd and /bin/cmd bypass lookup. An
** unset or empty PATH means one empty entry, and an empty entry names
** the current directory, so `unset PATH; cd /bin; ls` still runs. */
char	*resolve_command(char *cmd, t_shell *sh)
{
	char	*path_env;
	char	**paths;
	char	*result;

	if (!cmd || !*cmd)
		return (NULL);
	if (ft_strchr(cmd, '/'))
		return (ft_strdup(cmd));
	path_env = env_get(sh->env, "PATH");
	if (!path_env || !*path_env)
		path_env = ".";
	paths = ft_split(path_env, ':');
	if (!paths)
		return (NULL);
	result = search_in_path(cmd, paths);
	free_split(paths);
	return (result);
}
