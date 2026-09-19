/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   expander_utils.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/10 15:37:25 by malkilan          #+#    #+#             */
/*   Updated: 2026/09/10 16:05:48 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

int	var_name_len(const char *s, const char *quotes)
{
	int	len;

	if (!ft_isalpha(*s) && *s != '_')
		return (0);
	if (quotes[0] & Q_BREAK)
		return (0);
	len = 1;
	while (s[len] && (ft_isalnum(s[len]) || s[len] == '_')
		&& !(quotes[len] & Q_BREAK))
		len++;
	return (len);
}

char	*var_lookup(const char *name, t_shell *sh)
{
	char	*value;

	if (!ft_strncmp(name, "?", 2))
		return (ft_itoa(sh->last_status));
	value = env_get(sh->env, name);
	if (!value)
		return (ft_strdup(""));
	return (ft_strdup(value));
}

char	*join_free(char *dst, char *src)
{
	char	*joined;

	if (!dst || !src)
	{
		free(dst);
		free(src);
		return (NULL);
	}
	joined = ft_strjoin(dst, src);
	free(dst);
	free(src);
	return (joined);
}

char	*pad_new(size_t n, char flag)
{
	char	*pad;

	pad = malloc(n + 1);
	if (!pad)
		return (NULL);
	ft_memset(pad, flag, n);
	pad[n] = '\0';
	return (pad);
}

void	exp_append(t_exp *e, char *chunk, char flag)
{
	char	*pad;

	pad = NULL;
	if (chunk)
		pad = pad_new(ft_strlen(chunk), flag);
	e->out = join_free(e->out, chunk);
	e->mask = join_free(e->mask, pad);
}
