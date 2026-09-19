/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   exec_path.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/07 19:39:51 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/09/07 22:06:56 by rabdalqa         ###   ########.fr       */
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

static int	is_executable_file(const char *path)
{
	struct stat	st;

	if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
		return (0);
	return (access(path, X_OK) == 0);
}

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
