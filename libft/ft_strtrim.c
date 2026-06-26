/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strtrim.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/19 18:29:35 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/23 14:14:38 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

char	*ft_strtrim(char const *s1, char const *set)
{
	size_t	i;
	size_t	j;
	size_t	k;

	i = 0;
	while (s1[i] != '\0')
	{
		j = 0;
		while (set[j] && set[j] != s1[i])
			j++;
		if (set[j] == '\0')
			break ;
		i++;
	}
	k = ft_strlen(s1);
	while (k - 1 > i)
	{
		j = 0;
		while (set[j] != s1[k - 1] && set[j])
			j++;
		if (set[j] == '\0')
			break ;
		k--;
	}
	return (ft_substr(s1, i, k - i));
}
