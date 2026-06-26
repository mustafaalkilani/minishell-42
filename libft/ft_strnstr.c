/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strnstr.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mustafa <mustafa@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/12 17:30:02 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/26 17:06:57 by mustafa          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

// #include <stdio.h>

char	*ft_strnstr(const char *big, const char *little, size_t len)
{
	size_t	i;
	size_t	j;

	if (*little == '\0')
		return ((char *)big);
	i = 0;
	while (big[i] && i < len)
	{
		j = 0;
		while (big[i + j] && little[j] && big[i + j] == little[j] && i
			+ j < len)
			j++;
		if (little[j] == '\0')
			return ((char *)&big[i]);
		i++;
	}
	return (NULL);
}

// int main() {
//     const char *text = "The quick brown fox jumps over the lazy dog";
//     const char *search = "fox";

//     // Search only in first 15 characters - "fox" appears after position 16
//     char *result = ft_strnstr(text, search, 15);
//     printf("%s", result);

//     // Now search in first 20 characters - "fox" is at position 16
//     result = ft_strnstr(text, search, 20);
//     printf("\n%s", result);

//     return (0);

// }
