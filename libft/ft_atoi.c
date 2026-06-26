/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_atoi.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/21 16:48:33 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/25 17:51:14 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

// #include <stdio.h>

#include "libft.h"

int	ft_atoi(const char *nptr)
{
	int	num;
	int	i;
	int	sign;

	num = 0;
	i = 0;
	sign = 1;
	while ((nptr[i] >= 9 && nptr[i] <= 13) || nptr[i] == 32)
		i++;
	if (nptr[i] == '-' && (nptr[i + 1] >= '0' && nptr[i + 1] <= '9'))
	{
		sign = -1;
		i++;
	}
	if (nptr[i] == '+' && nptr[i + 1] >= '0' && nptr[i + 1] <= '9')
		i++;
	while (nptr[i] >= '0' && nptr[i] <= '9')
	{
		num = num * 10 + (nptr[i] - '0');
		i++;
	}
	return (sign * num);
}
