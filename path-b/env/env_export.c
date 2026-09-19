/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   env_export.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/06 17:06:53 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/09/06 19:08:17 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

static char	*make_pair(t_env *node)
{
	char	*head;
	char	*pair;

	head = ft_strjoin(node->key, "=");
	if (!head)
		return (NULL);
	pair = ft_strjoin(head, node->value);
	free(head);
	return (pair);
}

static int	env_export_count(t_env *env)
{
	int	n;

	n = 0;
	while (env)
	{
		if (env->value)
			n++;
		env = env->next;
	}
	return (n);
}

char	**env_to_envp(t_env *env)
{
	char	**envp;
	int		i;

	envp = ft_calloc((size_t)env_export_count(env) + 1, sizeof(char *));
	if (!envp)
		return (NULL);
	i = 0;
	while (env)
	{
		if (env->value)
		{
			envp[i] = make_pair(env);
			if (!envp[i])
			{
				free_split(envp);
				return (NULL);
			}
			i++;
		}
		env = env->next;
	}
	return (envp);
}

int	env_key_is_valid(const char *key)
{
	int	i;

	if (!key || !key[0])
		return (0);
	if (!ft_isalpha(key[0]) && key[0] != '_')
		return (0);
	i = 1;
	while (key[i] && key[i] != '=')
	{
		if (key[i] == '+' && key[i + 1] == '=')
			return (1);
		if (!ft_isalnum(key[i]) && key[i] != '_')
			return (0);
		i++;
	}
	return (1);
}

int	env_append(t_env **env, char *key, const char *add)
{
	char	*old;
	char	*joined;
	int		rc;

	key[ft_strlen(key) - 1] = '\0';
	old = env_get(*env, key);
	if (!old)
		old = "";
	joined = ft_strjoin(old, add);
	if (!joined)
		return (-1);
	rc = env_set(env, key, joined);
	free(joined);
	return (rc);
}
