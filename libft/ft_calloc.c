/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_calloc.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/19 14:15:59 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/25 18:43:38 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

// #include <stdio.h>
void	*ft_calloc(size_t nmemb, size_t size)
{
	void	*ptr;
	size_t	total;

	total = nmemb * size;
	ptr = malloc(total);
	if (!ptr)
		return (NULL);
	ft_bzero(ptr, total);
	return (ptr);
}

// int main(void)
// {
//     int *ptr;

//     ptr = (int *)ft_calloc(5, sizeof(int));
//     for (int i = 0; i < 5; i++)
//         printf("%d", ptr[i]);
// }