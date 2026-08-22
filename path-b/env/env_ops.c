/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   env_ops.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

t_env	*env_find(t_env *env, const char *key)
{
	while (env)
	{
		if (!ft_strncmp(env->key, key, ft_strlen(key) + 1))
			return (env);
		env = env->next;
	}
	return (NULL);
}

/* Returns NULL both for "not present" and for "declared without value".
** Callers treat both as empty, which is what bash does on expansion. */
char	*env_get(t_env *env, const char *key)
{
	t_env	*node;

	node = env_find(env, key);
	if (!node)
		return (NULL);
	return (node->value);
}

/* Overwrites an existing key in place so export order is preserved,
** which matters because bash lists exported variables sorted but keeps
** insertion order for `env`. */
int	env_set(t_env **env, const char *key, const char *value)
{
	t_env	*node;
	char	*copy;

	node = env_find(*env, key);
	if (node)
	{
		if (!value)
			return (0);
		copy = ft_strdup(value);
		if (!copy)
			return (-1);
		free(node->value);
		node->value = copy;
		return (0);
	}
	node = env_new(key, value);
	if (!node)
		return (-1);
	env_add_back(env, node);
	return (0);
}

int	env_unset(t_env **env, const char *key)
{
	t_env	*cur;
	t_env	*prev;

	cur = *env;
	prev = NULL;
	while (cur)
	{
		if (!ft_strncmp(cur->key, key, ft_strlen(key) + 1))
		{
			if (prev)
				prev->next = cur->next;
			else
				*env = cur->next;
			free(cur->key);
			free(cur->value);
			free(cur);
			return (0);
		}
		prev = cur;
		cur = cur->next;
	}
	return (0);
}

void	env_free(t_env *env)
{
	t_env	*next;

	while (env)
	{
		next = env->next;
		free(env->key);
		free(env->value);
		free(env);
		env = next;
	}
}
