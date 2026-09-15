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

/* A variable name is [A-Za-z_][A-Za-z0-9_]*. Returns 0 when the $ is not
** followed by a valid name, in which case the $ stays literal, matching
** bash for inputs like "$", "$ ", "$1abc" is $1 then abc.
** A quote boundary also ends the name: $T"o" reads T, not To, and it
** ends it before it starts for "$"USER, which is a literal $ then USER. */
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

/* $? is resolved here rather than through the env list because it is not
** an environment variable: it is shell state that never appears in env. */
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

/* Appends src to dst and frees dst. Used to build the expanded string
** incrementally; returns NULL on allocation failure after freeing both
** so the caller only has to test for NULL. */
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

/* A run of n identical flag bytes, so the mask can grow by exactly as
** much as the text it describes. */
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

/* Grows text and mask together so they stay the same length. chunk is
** consumed. A NULL anywhere poisons both sides, which lets the caller
** test only e->out once at the end. */
void	exp_append(t_exp *e, char *chunk, char flag)
{
	char	*pad;

	pad = NULL;
	if (chunk)
		pad = pad_new(ft_strlen(chunk), flag);
	e->out = join_free(e->out, chunk);
	e->mask = join_free(e->mask, pad);
}
